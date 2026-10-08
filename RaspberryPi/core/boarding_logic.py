"""
boarding_logic.py — F1 + F2 upgrade
  F1: manual_attendance cho học sinh & phụ xe quên thẻ
  F2: RFID pending queue (quẹt trước → validate → ngồi ghế → type 11)
"""
import math
import time
import threading
from collections import deque
from dataclasses import dataclass, field
from typing import Optional
from config import config
from services.logger import get_logger

logger = get_logger("BoardingLogic")


# ─────────────────────────────────────────────────────────────────────────────
# F2: Dataclass lưu thẻ RFID đang chờ xử lý
# ─────────────────────────────────────────────────────────────────────────────
@dataclass
class PendingRFID:
    rfid_code: str
    received_at: float          # unix timestamp khi Pi nhận mã
    server_validated: bool = False
    is_attendant: bool = False  # True nếu Server xác nhận là phụ xe


class BoardingLogic:
    def __init__(self, uart_thread, mqtt_client):
        self.uart = uart_thread
        self.mqtt = mqtt_client

        # Geofence parameters
        self.school_lat = config.SCHOOL_GEOFENCE_LAT
        self.school_lon = config.SCHOOL_GEOFENCE_LON
        self.school_radius = config.SCHOOL_GEOFENCE_RADIUS_M

        # State
        self.at_school = False
        self.trip_phase = "PICKUP"
        self.is_last_student_picked = False

        # Tracking student lists
        self.students_onboard = {}   # {rfid: seat_num}
        self.seat_student_map = {}   # {seat_num_str: rfid}

        # Critical alert state
        self.child_alone_active = False
        self.child_alone_start_time = None

        # Last known seats
        self.current_seats = {}

        # ── F1: Manual attendance counters ───────────────────────────────────
        self.manual_student_count = 0      # số học sinh được điểm danh thủ công
        self.manual_attendant_boarded = False  # phụ xe đã được điểm danh thủ công

        # ── F2: RFID pending queue ───────────────────────────────────────────
        self.pending_rfid_queue: deque[PendingRFID] = deque()
        self._pending_lock = threading.Lock()
        self._ttl = config.RFID_PENDING_TTL_SEC  # mặc định 300s

        # Bắt đầu background timer kiểm tra TTL
        self._start_ttl_watchdog()

    # ─────────────────────────────────────────────────────────────────────────
    # Helpers
    # ─────────────────────────────────────────────────────────────────────────

    def set_last_student_picked(self, val: bool):
        self.is_last_student_picked = val
        logger.info("is_last_student_picked updated to: %s", val)

    def calculate_distance(self, lat1, lon1, lat2, lon2) -> float:
        R = 6371000.0
        phi1 = math.radians(lat1)
        phi2 = math.radians(lat2)
        dphi = math.radians(lat2 - lat1)
        dlambda = math.radians(lon2 - lon1)
        a = math.sin(dphi / 2.0) ** 2 + math.cos(phi1) * math.cos(phi2) * math.sin(dlambda / 2.0) ** 2
        return R * 2 * math.atan2(math.sqrt(a), math.sqrt(1 - a))

    # ─────────────────────────────────────────────────────────────────────────
    # GPS / trip phase
    # ─────────────────────────────────────────────────────────────────────────

    def update_gps(self, lat: float, lon: float, speed: float):
        dist = self.calculate_distance(lat, lon, self.school_lat, self.school_lon)
        previously_at_school = self.at_school
        self.at_school = (dist <= self.school_radius)

        if self.at_school and not previously_at_school:
            logger.info("Vehicle entered school geofence (Distance: %.1fm)", dist)
            if self.trip_phase == "PICKUP":
                self.trip_phase = "ARRIVE_SCHOOL"
                logger.info("Trip phase transitioned to: ARRIVE_SCHOOL")

        elif not self.at_school and previously_at_school:
            logger.info("Vehicle left school geofence (Distance: %.1fm)", dist)
            if self.trip_phase == "DEPART_SCHOOL":
                self.trip_phase = "DROPOFF"
                logger.info("Trip phase transitioned to: DROPOFF")

    def set_trip_phase(self, phase: str):
        if phase in ("PICKUP", "ARRIVE_SCHOOL", "DEPART_SCHOOL", "DROPOFF"):
            self.trip_phase = phase
            logger.info("Trip phase manually set to: %s", phase)
            if phase == "DEPART_SCHOOL":
                self.students_onboard.clear()
                self.seat_student_map.clear()

    # ─────────────────────────────────────────────────────────────────────────
    # Seat updates
    # ─────────────────────────────────────────────────────────────────────────

    def update_seats(self, seats: dict):
        self.current_seats = seats
        self.check_child_alone_safety(seats)

        if self.trip_phase == "ARRIVE_SCHOOL":
            self.check_bulk_alight(seats)
        elif self.trip_phase == "DEPART_SCHOOL":
            self.check_bulk_board(seats)

        # ── F2: khi ghế 0→1 thử ghép RFID đang chờ ─────────────────────────
        for i in range(3, 17):
            seat_str = str(i)
            prev = self.seat_student_map.get(seat_str)
            if seats.get(seat_str, 0) == 1 and prev is None:
                # Ghế vừa có người — thử finalize boarding
                self._try_finalize_boarding(i)

    # ─────────────────────────────────────────────────────────────────────────
    # F2: RFID Pending Queue
    # ─────────────────────────────────────────────────────────────────────────

    def add_pending_rfid(self, rfid_code: str):
        """Gọi khi Pi nhận frame 0xF2 từ Master (RFID vừa quẹt).
        Đẩy vào queue chờ Server validate. KHÔNG gửi type 11 ngay."""
        with self._pending_lock:
            # Kiểm tra xem đã có mã này trong queue chưa (spam guard)
            for p in self.pending_rfid_queue:
                if p.rfid_code == rfid_code:
                    logger.warning("RFID %s already in pending queue — spam ignored", rfid_code)
                    return
            entry = PendingRFID(rfid_code=rfid_code, received_at=time.time())
            self.pending_rfid_queue.append(entry)
            logger.info("RFID %s added to pending queue (TTL=%ds)", rfid_code, self._ttl)

    def mark_rfid_validated(self, rfid_code: str, is_attendant: bool = False):
        """Gọi sau khi Server xác nhận RFID hợp lệ (type rfid_check_ack result=1)."""
        with self._pending_lock:
            for p in self.pending_rfid_queue:
                if p.rfid_code == rfid_code:
                    p.server_validated = True
                    p.is_attendant = is_attendant
                    logger.info("RFID %s marked as validated (is_attendant=%s)", rfid_code, is_attendant)
                    return
        logger.warning("mark_rfid_validated: RFID %s not found in pending queue", rfid_code)

    def _try_finalize_boarding(self, seat_number: int):
        """Gọi khi ghế seat_number vừa chuyển 0→1.
        Lấy phần tử đầu FIFO đã validated → ghép với ghế → gửi type 11."""
        with self._pending_lock:
            validated_entry: Optional[PendingRFID] = None
            for p in self.pending_rfid_queue:
                if p.server_validated and not p.is_attendant:
                    validated_entry = p
                    break
            if validated_entry is None:
                logger.debug("Seat %d occupied but no validated RFID pending — skipping finalize", seat_number)
                return
            self.pending_rfid_queue.remove(validated_entry)

        rfid = validated_entry.rfid_code
        phase_map = {"PICKUP": 1, "ARRIVE_SCHOOL": 2, "DEPART_SCHOOL": 3, "DROPOFF": 4}
        phase_num = phase_map.get(self.trip_phase, 1)

        # Ghi nhận nội bộ
        self.students_onboard[rfid] = seat_number
        self.seat_student_map[str(seat_number)] = rfid

        logger.info("F2 Finalize boarding: RFID=%s mapped to seat %d (phase=%s)", rfid, seat_number, self.trip_phase)

        # Gửi type 11 lên Server (thời điểm ngồi vào ghế)
        self.mqtt.publish_message(11, {
            "rfid_code": rfid,
            "seat_number": seat_number,
            "trip_phase": phase_num,
            "action": "board"
        }, priority=1)

        # Nếu là học sinh cuối cùng chiều đón
        if self.is_last_student_picked:
            self.uart.send_frame(0x05, 0x03)
            self.mqtt.publish_message(12, {
                "event_code": 503,
                "students_onboard": len(self.students_onboard)
            }, priority=1)

    def _start_ttl_watchdog(self):
        """Background thread: quét queue mỗi 30s, xoá entry hết TTL, phát 05/009."""
        def watchdog():
            while True:
                time.sleep(30)
                now = time.time()
                expired = []
                with self._pending_lock:
                    for p in list(self.pending_rfid_queue):
                        if now - p.received_at >= self._ttl:
                            expired.append(p)
                    for p in expired:
                        self.pending_rfid_queue.remove(p)
                for p in expired:
                    logger.warning("RFID %s expired from pending queue (TTL=%ds) — phát 05/009", p.rfid_code, self._ttl)
                    self.uart.send_frame(0x05, 0x09)  # 05/009: Thẻ hết hạn

        t = threading.Thread(target=watchdog, daemon=True, name="RFID-TTL-Watchdog")
        t.start()

    # ─────────────────────────────────────────────────────────────────────────
    # RFID scan (luồng cũ — giữ cho chiều DROPOFF alight + attendant RFID thật)
    # Với F2, chiều BOARDING không đi qua đây nữa (đã qua _try_finalize_boarding)
    # ─────────────────────────────────────────────────────────────────────────

    def handle_rfid_scan(self, rfid: str):
        """Xử lý RFID nhận được từ UART 0xF2.
        F2: Thêm vào pending queue, KHÔNG gửi type 11 ngay.
        Chiều DROPOFF (alight): vẫn xử lý trực tiếp như cũ."""
        # Nếu học sinh đã có trên xe → ALIGHT (chiều trả)
        if rfid in self.students_onboard:
            self._handle_student_alight(rfid)
            return

        # Ngược lại: push vào pending queue (F2)
        self.add_pending_rfid(rfid)

        # Gửi mã RFID lên Server để validate (qua MQTT topic rfid_check)
        # Server sẽ trả kết quả qua type 20 action="rfid_check_ack"
        self.mqtt.publish_message(11, {
            "rfid_code": rfid,
            "action": "validate_only"   # Tín hiệu cho Server biết chỉ validate, chưa finalize
        }, priority=1)
        logger.info("F2: RFID %s sent to Server for validation", rfid)

    def _handle_student_alight(self, rfid: str):
        """Học sinh đã có trên xe quẹt thẻ → xuống xe."""
        phase_map = {"PICKUP": 1, "ARRIVE_SCHOOL": 2, "DEPART_SCHOOL": 3, "DROPOFF": 4}
        phase_num = phase_map.get(self.trip_phase, 1)

        seat_val = self.students_onboard.pop(rfid, None)
        if seat_val is not None and str(seat_val) in self.seat_student_map:
            del self.seat_student_map[str(seat_val)]

        remaining = len(self.students_onboard)
        logger.info("Student %s alighted from seat %s (Remaining: %d)", rfid, seat_val, remaining)

        self.mqtt.publish_message(11, {
            "rfid_code": rfid,
            "seat_number": seat_val if seat_val else 0,
            "trip_phase": phase_num,
            "action": "alight"
        }, priority=1)

        self.uart.send_frame(0x05, 0x01)  # 05/001

        if remaining == 0:
            logger.info("All students alighted — 05/004")
            self.uart.send_frame(0x05, 0x04)
            self.mqtt.publish_message(12, {"event_code": 504, "students_onboard": 0}, priority=1)

    # ─────────────────────────────────────────────────────────────────────────
    # Child alone safety
    # ─────────────────────────────────────────────────────────────────────────

    def check_child_alone_safety(self, seats: dict):
        driver_present = seats.get("1", 0) == 1
        # F1: phụ xe được điểm danh thủ công cũng coi như "có mặt"
        attendant_present = seats.get("2", 0) == 1 or self.manual_attendant_boarded

        student_onboard = False
        occupied_student_seats = []
        for i in range(3, 17):
            if seats.get(str(i), 0) == 1:
                student_onboard = True
                occupied_student_seats.append(i)

        if student_onboard and not driver_present and not attendant_present:
            if self.child_alone_start_time is None:
                self.child_alone_start_time = time.time()
                logger.warning("Safety Hazard: Student alone — starting 15s timer…")
            else:
                elapsed = time.time() - self.child_alone_start_time
                if elapsed >= 15 and not self.child_alone_active:
                    self.child_alone_active = True
                    logger.critical("SAFETY CRITICAL: CHILD LEFT ALONE!")
                    self.uart.send_frame(0x08, 0x01)
                    self.mqtt.publish_message(16, {
                        "occupied_student_seats": occupied_student_seats,
                        "driver_seat": 0,
                        "attendant_seat": 0,
                        "duration_sec": int(elapsed)
                    }, priority=2)
                elif self.child_alone_active and int(elapsed) % 10 == 0:
                    self.mqtt.publish_message(16, {
                        "occupied_student_seats": occupied_student_seats,
                        "driver_seat": 0,
                        "attendant_seat": 0,
                        "duration_sec": int(elapsed)
                    }, priority=2)
        else:
            if self.child_alone_start_time is not None:
                logger.info("Safety condition cleared — silencing 08/001")
                self.child_alone_start_time = None
                if self.child_alone_active:
                    self.child_alone_active = False
                    self.uart.send_frame(0xF4, 0x00)

    # ─────────────────────────────────────────────────────────────────────────
    # Bulk alight / board (trường)
    # ─────────────────────────────────────────────────────────────────────────

    def check_bulk_alight(self, seats: dict):
        student_count = sum(1 for i in range(3, 17) if seats.get(str(i), 0) == 1)
        if student_count == 0 and len(self.seat_student_map) > 0:
            logger.info("Bulk alight at school detected.")
            expected = len(self.seat_student_map)
            self.mqtt.publish_message(12, {
                "event_code": 506,
                "seats_emptied": expected,
                "expected_boarded": expected,
                "match": True
            }, priority=1)
            self.uart.send_frame(0x05, 0x06)
            self.students_onboard.clear()
            self.seat_student_map.clear()
            # Reset manual counters khi bắt đầu pha mới
            self.manual_student_count = 0

    def check_bulk_board(self, seats: dict):
        student_count = sum(1 for i in range(3, 17) if seats.get(str(i), 0) == 1)
        if student_count > 0 and len(self.seat_student_map) == 0:
            for i in range(3, 17):
                seat_num = str(i)
                if seats.get(seat_num, 0) == 1:
                    stub_rfid = f"STUB_{i}"
                    self.students_onboard[stub_rfid] = i
                    self.seat_student_map[seat_num] = stub_rfid
            logger.info("Bulk boarding at school: %d stubs loaded.", student_count)
            self.mqtt.publish_message(12, {
                "event_code": 507,
                "students_onboard": student_count
            }, priority=1)

    # ─────────────────────────────────────────────────────────────────────────
    # F1: Manual attendance — học sinh & phụ xe quên thẻ
    # ─────────────────────────────────────────────────────────────────────────

    def handle_manual_attendance(self, payload: dict) -> bool:
        """Xử lý lệnh điểm danh thủ công từ Server (type=20, action='manual_attendance').
        payload chứa: target_type, target_id, rfid_code, seat_number, direction, trip_phase.
        Trả True nếu xử lý thành công, False nếu từ chối."""
        target_type = payload.get("target_type", "student")
        target_id   = payload.get("target_id", "")
        rfid_code   = payload.get("rfid_code", "")
        seat_number = int(payload.get("seat_number", 0))
        direction   = payload.get("direction", "board")  # "board" | "alight"

        logger.info("Manual attendance: target_type=%s id=%s dir=%s seat=%d",
                    target_type, target_id, direction, seat_number)

        if target_type == "student":
            return self._manual_student(rfid_code, target_id, seat_number, direction)
        elif target_type == "attendant":
            return self._manual_attendant(rfid_code, target_id, seat_number, direction)
        else:
            logger.error("handle_manual_attendance: unknown target_type=%s", target_type)
            return False

    def _manual_student(self, rfid_code: str, student_id: str, seat_number: int, direction: str) -> bool:
        """F1 — Học sinh quên thẻ: điểm danh thủ công."""
        if direction == "board":
            # Kiểm tra học sinh chưa có trên xe
            if rfid_code in self.students_onboard:
                logger.warning("Manual board: student %s already onboard — rejected", student_id)
                return False

            # Ghi nhận nội bộ (dùng rfid_code làm key nếu có, fallback student_id)
            key = rfid_code if rfid_code else f"MANUAL_{student_id}"
            use_seat = seat_number if seat_number > 0 else self._find_free_student_seat()
            self.students_onboard[key] = use_seat
            if use_seat > 0:
                self.seat_student_map[str(use_seat)] = key
            self.manual_student_count += 1

            logger.info("Manual board: student %s (rfid=%s) at seat %d. manual_count=%d",
                        student_id, key, use_seat, self.manual_student_count)

            # Âm thanh 05/001 — thông báo học sinh được điểm danh
            self.uart.send_frame(0x05, 0x01)
            return True

        elif direction == "alight":
            key = rfid_code if rfid_code else f"MANUAL_{student_id}"
            # Đối chiếu cảm biến ghế: nếu ghế vẫn có người → từ chối
            if seat_number > 0 and self.current_seats.get(str(seat_number), 0) == 1:
                logger.warning("Manual alight rejected: seat %d still occupied by sensor", seat_number)
                return False

            if key in self.students_onboard:
                seat_val = self.students_onboard.pop(key, None)
                if seat_val and str(seat_val) in self.seat_student_map:
                    del self.seat_student_map[str(seat_val)]
                self.manual_student_count = max(0, self.manual_student_count - 1)
                logger.info("Manual alight: student %s removed. manual_count=%d",
                            student_id, self.manual_student_count)
            return True

        return False

    def _manual_attendant(self, rfid_code: str, attendant_id: str, seat_number: int, direction: str) -> bool:
        """F1 — Phụ xe quên thẻ: điểm danh thủ công.
        seat_number luôn = 2 (ghế phụ xe cố định)."""
        if direction == "board":
            if self.manual_attendant_boarded:
                logger.warning("Manual attendant board: already boarded — rejected")
                return False
            self.manual_attendant_boarded = True
            logger.info("Manual attendant board: attendant %s confirmed.", attendant_id)
            # Âm thanh 03/001 — phụ xe login thành công
            self.uart.send_frame(0x03, 0x01)
            return True

        elif direction == "alight":
            # Đối chiếu ghế 2: nếu ghế 2 vẫn có người → từ chối
            if self.current_seats.get("2", 0) == 1:
                logger.warning("Manual attendant alight rejected: seat 2 still occupied")
                return False
            self.manual_attendant_boarded = False
            logger.info("Manual attendant alight: attendant %s logged out.", attendant_id)
            # Âm thanh 04/001 — phụ xe logout thành công
            self.uart.send_frame(0x04, 0x01)
            return True

        return False

    def _find_free_student_seat(self) -> int:
        """Tìm ghế học sinh (3-16) đang occupied nhưng chưa có rfid map."""
        for i in range(3, 17):
            seat_str = str(i)
            if self.current_seats.get(seat_str, 0) == 1 and seat_str not in self.seat_student_map:
                return i
        return 0

    # ─────────────────────────────────────────────────────────────────────────
    # Helpers cho session_manager (kiểm tra điều kiện logout)
    # ─────────────────────────────────────────────────────────────────────────

    @property
    def total_students_onboard(self) -> int:
        """Tổng học sinh trên xe (RFID + thủ công)."""
        return len(self.students_onboard)

    @property
    def seats_without_rfid_count(self) -> int:
        """Số ghế có người nhưng chưa quẹt thẻ (trừ phần thủ công)."""
        occupied = sum(1 for i in range(3, 17) if self.current_seats.get(str(i), 0) == 1)
        mapped = len(self.seat_student_map)
        gap = occupied - (mapped + self.manual_student_count)
        return max(0, gap)
