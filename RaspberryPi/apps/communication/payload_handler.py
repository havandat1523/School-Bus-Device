"""
payload_handler.py — Xử lý payload Server → Pi (type=20) & Quản lý SOS (F3)
  F1: action="manual_attendance"  → học sinh / phụ xe quên thẻ
  F2: action="rfid_check_ack"     → Server validate RFID kết quả
  F3: action="sos_ack"            → Server / Admin xác nhận SOS
      trigger_sos / cancel_sos    → Quản lý luồng SOS đa nguồn & SIM Call
"""
import uuid
import time
import threading
from typing import Optional
from config import config
from services.logger import get_logger
from apps.sim_manager.sim_manager import SimManager

logger = get_logger("PayloadHandler")


class PayloadHandler:
    """
    Được gọi từ mqtt_client khi nhận payload từ Server (type=20),
    đồng thời quản lý vòng đời phát tín hiệu SOS (F3) và retry logic.
    """

    def __init__(self, boarding_logic, uart_thread, mqtt_client, session_manager=None, sim_manager=None):
        self.boarding = boarding_logic
        self.uart = uart_thread
        self.mqtt = mqtt_client
        self.session = session_manager
        self.sim_manager = sim_manager or SimManager()

        # F3: SOS State
        self._sos_lock = threading.Lock()
        self._active_sos_id: Optional[str] = None
        self._sos_retry_count: int = 0
        self._sos_retry_timer: Optional[threading.Timer] = None
        self._sos_payload: Optional[dict] = None
        self._sos_acked: bool = False

    # ─────────────────────────────────────────────────────────────────────────
    # Entry point chính — gọi khi nhận type=20 từ Server
    # ─────────────────────────────────────────────────────────────────────────

    def handle_server_command(self, data: dict, vid: str):
        """Xử lý type=20 (server_command) từ Server."""
        action = data.get("action", "")

        if action == "manual_attendance":
            self._handle_manual_attendance(data, vid)

        elif action == "rfid_check_ack":
            self._handle_rfid_check_ack(data, vid)

        elif action == "sos_ack":
            self._handle_sos_ack(data, vid)

        elif action == "stream_status":
            pass  # Xử lý ở module streaming

        else:
            logger.warning("Unknown server_command action: %s", action)

    # ─────────────────────────────────────────────────────────────────────────
    # F1: Manual attendance
    # ─────────────────────────────────────────────────────────────────────────

    def _handle_manual_attendance(self, data: dict, vid: str):
        """Server xác nhận học sinh/phụ xe được điểm danh thủ công."""
        target_type = data.get("target_type", "student")
        target_id   = data.get("target_id", "")
        direction   = data.get("direction", "board")

        logger.info("F1 manual_attendance: target_type=%s id=%s direction=%s", target_type, target_id, direction)

        success = self.boarding.handle_manual_attendance(data)

        if success:
            logger.info("F1 manual_attendance: processed OK → sending ACK to Server")
            self.mqtt.publish_message(20, {
                "action": "manual_attendance_ack",
                "target_type": target_type,
                "target_id": target_id,
                "direction": direction,
                "result": 1
            }, priority=1)
        else:
            logger.warning("F1 manual_attendance: REJECTED for %s %s dir=%s", target_type, target_id, direction)
            self.mqtt.publish_message(20, {
                "action": "manual_attendance_ack",
                "target_type": target_type,
                "target_id": target_id,
                "direction": direction,
                "result": 0,
                "reason": "rejected_by_device"
            }, priority=1)

    # ─────────────────────────────────────────────────────────────────────────
    # F2: RFID validate ACK từ Server
    # ─────────────────────────────────────────────────────────────────────────

    def _handle_rfid_check_ack(self, data: dict, vid: str):
        """Server trả kết quả validate RFID."""
        rfid_code   = data.get("rfid_code", "")
        result      = data.get("result", 0)
        is_attendant = data.get("is_attendant", False)

        if result == 1:
            logger.info("F2 rfid_check_ack: RFID %s VALID (is_attendant=%s)", rfid_code, is_attendant)
            self.boarding.mark_rfid_validated(rfid_code, is_attendant=is_attendant)

            if is_attendant:
                self.uart.send_frame(0x03, 0x01)
                logger.info("F2: Attendant RFID valid — 03/001 played")
            else:
                self.uart.send_frame(0x05, 0x08)
                logger.info("F2: Student RFID valid — 05/008 played, waiting for seat")

        else:
            reason = data.get("reason", "invalid")
            logger.warning("F2 rfid_check_ack: RFID %s INVALID (reason=%s)", rfid_code, reason)
            self.uart.send_frame(0x05, 0x02)

            with self.boarding._pending_lock:
                for p in list(self.boarding.pending_rfid_queue):
                    if p.rfid_code == rfid_code:
                        self.boarding.pending_rfid_queue.remove(p)
                        logger.info("F2: RFID %s removed from pending queue (invalid)", rfid_code)
                        break

    # ─────────────────────────────────────────────────────────────────────────
    # F3: SOS Nâng cao — Trigger & Retry logic
    # ─────────────────────────────────────────────────────────────────────────

    def trigger_sos(self, triggered_by: int = 1, lat: float = None, lon: float = None, seat_number: int = 1):
        """
        Kích hoạt sự kiện SOS:
          triggered_by: 1=slave_button, 2=ui_button, 3=gpio_button
        """
        with self._sos_lock:
            # Tạo sos_id UUID v4 duy nhất
            sos_id = str(uuid.uuid4())
            self._active_sos_id = sos_id
            self._sos_retry_count = 0
            self._sos_acked = False
            self._cancel_sos_retry_timer()

            use_lat = lat if lat is not None else getattr(self.boarding, "current_lat", config.SCHOOL_GEOFENCE_LAT)
            use_lon = lon if lon is not None else getattr(self.boarding, "current_lon", config.SCHOOL_GEOFENCE_LON)

            self._sos_payload = {
                "sos_id": sos_id,
                "lat": use_lat,
                "lon": use_lon,
                "triggered_by": triggered_by,
                "seat_number": seat_number
            }

            logger.warning("🚨 TRIGGER SOS: ID=%s Source=%d Lat=%.5f Lon=%.5f", sos_id, triggered_by, use_lat, use_lon)

            # Phân nhánh theo nguồn phát (bất đối xứng):
            if triggered_by == 1:
                # 1=Slave: Master ĐÃ tự phát 07/001 qua DFPlayer. Pi KHÔNG phát lại 07/001!
                # Kích hoạt module SIM A7680s gọi điện thoại khẩn cấp
                driver_phone = getattr(self.session, "driver_phone_number", None) if self.session else None
                logger.info("F3: Slave SOS triggered — activating SimManager call loop (Phone: %s)", driver_phone)
                self.sim_manager.start_call_loop(driver_phone)
            else:
                # 2=UI hoặc 3=GPIO: Pi gửi frame 07/001 xuống Master (Master không tự phát)
                # KHÔNG kích hoạt cuộc gọi SIM
                logger.info("F3: UI/GPIO SOS triggered — playing 07/001 on Master (no SIM call)")
                self.uart.send_frame(0x07, 0x01)

            # Gửi gói type 15 lần 1 lên Server
            self.mqtt.publish_message(15, self._sos_payload, priority=2)

            # Bắt đầu timer 60s chờ ACK stage=0 từ Server
            self._schedule_sos_retry(config.SOS_RETRY_INTERVAL_SEC)

    def cancel_sos(self):
        """Hủy trạng thái SOS khi người dùng nhấn hủy hoặc nhận CAN 0x101."""
        with self._sos_lock:
            logger.info("🚨 SOS CANCELLED by user/device")
            self._cancel_sos_retry_timer()
            self._active_sos_id = None
            self._sos_payload = None

            # Dừng cuộc gọi SIM ngay lập tức
            self.sim_manager.stop()

            # Gửi frame huỷ cảnh báo âm thanh xuống Master (0xF4 0x00)
            self.uart.send_frame(0xF4, 0x00)

    def _handle_sos_ack(self, data: dict, vid: str):
        """Xử lý phản hồi ACK cho sự kiện SOS từ Server."""
        sos_id = data.get("sos_id", "")
        stage  = data.get("stage", 0)

        with self._sos_lock:
            if self._active_sos_id and sos_id != self._active_sos_id:
                logger.warning("Received sos_ack for different sos_id: %s (current: %s)", sos_id, self._active_sos_id)
                return

            if stage == 0:
                # Server đã nhận được SOS thành công
                self._sos_acked = True
                self._cancel_sos_retry_timer()
                logger.info("F3 sos_ack stage 0 received for %s — playing 07/002", sos_id)
                # 07/002: "Trường hợp khẩn cấp đã được gửi tới người quản trị"
                self.uart.send_frame(0x07, 0x02)

            elif stage == 1:
                # Quản trị viên đã bấm Acknowledge tiếp nhận trên Dashboard
                logger.warning("F3 sos_ack stage 1 received for %s: Admin acknowledged emergency!", sos_id)

    def _schedule_sos_retry(self, timeout_sec: int):
        """Lập lịch kiểm tra timeout ACK."""
        self._cancel_sos_retry_timer()
        self._sos_retry_timer = threading.Timer(timeout_sec, self._on_sos_timeout)
        self._sos_retry_timer.daemon = True
        self._sos_retry_timer.start()

    def _cancel_sos_retry_timer(self):
        if self._sos_retry_timer:
            self._sos_retry_timer.cancel()
            self._sos_retry_timer = None

    def _on_sos_timeout(self):
        """Hết 60s mà chưa nhận được ACK stage=0 từ Server."""
        with self._sos_lock:
            if not self._active_sos_id or self._sos_acked:
                return

            self._sos_retry_count += 1
            logger.warning("F3: SOS ACK stage 0 timeout (Retry %d/%d) for sos_id=%s",
                           self._sos_retry_count, config.SOS_RETRY_MAX, self._active_sos_id)

            # Phát 07/003: "Không thành công, đang thử lại"
            self.uart.send_frame(0x07, 0x03)
            time.sleep(1.0)

            if self._sos_retry_count < config.SOS_RETRY_MAX:
                # Phát 07/004: "Đang gửi lại server"
                self.uart.send_frame(0x07, 0x04)

                # Gửi lại bản tin type 15 (giữ nguyên sos_id để Server khử trùng lặp)
                if self._sos_payload:
                    logger.info("F3: Retransmitting SOS type 15 to Server (attempt %d)", self._sos_retry_count + 1)
                    self.mqtt.publish_message(15, self._sos_payload, priority=2)

                # Lập lịch timeout cho lần tiếp theo
                self._schedule_sos_retry(config.SOS_RETRY_INTERVAL_SEC)
            else:
                # Hết 5 lần retry loa: Phát 07/005 "Gửi cảnh báo khẩn cấp thất bại hoàn toàn"
                logger.error("F3: SOS reached MAX RETRIES (%d). Playing 07/005. Outbox will retain message.",
                             config.SOS_RETRY_MAX)
                self.uart.send_frame(0x07, 0x05)
                # Bản tin vẫn lưu trong Outbox manager để gửi bù khi có mạng (không gọi thêm loa)
