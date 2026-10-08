import serial
import threading
import time
import re
from PyQt5.QtCore import QThread, pyqtSignal
from config import config
from services.logger import get_logger

logger = get_logger("UART")

# Thứ tự ưu tiên port phần cứng trên Raspberry Pi 4B
# /dev/ttyAMA0  = UART0 (GPIO 14/15) sau khi tắt Bluetooth overlay
# /dev/serial0  = symlink tự động của RPi OS
# /dev/ttyUSBx  = UART-USB dongle dự phòng
PI_UART_CANDIDATES = [
    "/dev/ttyAMA0",
    "/dev/serial0",
    "/dev/ttyUSB0",
    "/dev/ttyUSB1",
]


def _detect_pi_uart_port() -> str:
    """
    Tự động chọn port UART phù hợp trên Raspberry Pi.
    Ưu tiên: .env / biến môi trường → danh sách ứng viên → fallback ttyAMA0.
    """
    import os
    env_port = config.UART_MASTER_PORT
    if os.path.exists(env_port):
        return env_port
    for candidate in PI_UART_CANDIDATES:
        if os.path.exists(candidate):
            logger.info("UART auto-detect: using %s (env was: %s)", candidate, env_port)
            return candidate
    return env_port

# F5: Bổ sung mã lỗi âm thanh & reason
EVENT_01_001 = (0x01, 0x01)  # Đăng nhập tài xế thành công
EVENT_01_002 = (0x01, 0x02)  # Đăng nhập tài xế thất bại chung (lỗi mạng, timeout...)
EVENT_01_003 = (0x01, 0x03)  # "Hệ thống đang bận, vui lòng thử lại" (đang PENDING_CONFIRM)
EVENT_01_004 = (0x01, 0x04)  # "Không thể xác nhận khuôn mặt tài xế" (Server result=0)

EVENT_02_001 = (0x02, 0x01)  # Đăng xuất tài xế thành công
EVENT_02_002 = (0x02, 0x02)  # Đăng xuất tài xế thất bại chung
EVENT_02_003 = (0x02, 0x03)  # "Không thể xác nhận khuôn mặt tài xế" (khi logout)
EVENT_02_004 = (0x02, 0x04)  # "Đăng xuất thất bại — còn học sinh hoặc phụ xe"

EVENT_03_001 = (0x03, 0x01)  # Phụ xe đăng nhập thành công
EVENT_04_001 = (0x04, 0x01)  # Phụ xe đăng xuất thành công

EVENT_05_001 = (0x05, 0x01)  # "Đã quẹt thẻ"
EVENT_05_002 = (0x05, 0x02)  # "Mã RFID không hợp lệ / lỗi quẹt thẻ" (hoặc spam)
EVENT_05_003 = (0x05, 0x03)  # Học sinh cuối cùng đã lên xe (về trường)
EVENT_05_004 = (0x05, 0x04)  # Học sinh cuối cùng đã xuống xe
EVENT_05_008 = (0x05, 0x08)  # "Mã RFID hợp lệ, xác nhận học sinh đã lên xe"
EVENT_05_009 = (0x05, 0x09)  # "Thẻ hết hạn, vui lòng quẹt lại" (TTL 5 phút)

class UARTThread(QThread):
    # ── Signals dữ liệu lên UI / Logic ──────────────────────────────────────
    gps_received      = pyqtSignal(float, float, float)  # lat, lon, speed_kmh
    seats_received    = pyqtSignal(dict)                 # {"1": 0/1 … "16": 0/1}
    rfid_received     = pyqtSignal(str)                  # RFID tag string
    sos_received        = pyqtSignal(int)                  # Nút SOS vật lý (1=slave, 2=ui, 3=gpio)
    sos_cancel_received = pyqtSignal()                     # Hủy SOS (CAN 0x101 từ Slave)
    dht11_received    = pyqtSignal(float, float)         # temperature, humidity
    # ── Signal trạng thái kết nối (True = đã kết nối, False = mất kết nối) ─
    connection_status = pyqtSignal(bool)

    def __init__(self):
        super().__init__()
        self.running   = False
        self.ser       = None
        self.lock      = threading.Lock()
        self._connected = False

        # State machine nhận frame
        self.state   = "STX"
        self.rx_main = 0
        self.rx_sub  = 0
        self.rx_len  = 0
        self.rx_data = bytearray()
        self.rx_chk  = 0

    # ────────────────────────────────────────────────────────────────────────
    # Kết nối / ngắt kết nối
    # ────────────────────────────────────────────────────────────────────────

    def open_port(self) -> bool:
        """
        Mở cổng serial.  Trả về True nếu thành công.
        Protocol: 115200 baud, 8N1, không flow-control — khớp USART1 STM32.
        """
        port = _detect_pi_uart_port()
        try:
            self.ser = serial.Serial(
                port=port,
                baudrate=config.UART_MASTER_BAUDRATE,   # 115200
                bytesize=serial.EIGHTBITS,
                parity=serial.PARITY_NONE,
                stopbits=serial.STOPBITS_ONE,
                xonxoff=False,
                rtscts=False,
                dsrdtr=False,
                timeout=0.1,
            )
            logger.info(
                "UART connected → %s @ %d baud (8N1)",
                port, config.UART_MASTER_BAUDRATE
            )
            self._set_connected(True)
            return True
        except serial.SerialException as e:
            logger.warning("UART open failed on %s: %s", port, e)
            self.ser = None
            self._set_connected(False)
            return False

    def _set_connected(self, state: bool):
        if self._connected != state:
            self._connected = state
            self.connection_status.emit(state)

    def _close_port(self):
        try:
            if self.ser and self.ser.is_open:
                self.ser.close()
        except Exception:
            pass
        self.ser = None
        self._set_connected(False)

    # ────────────────────────────────────────────────────────────────────────
    # Gửi frame xuống STM32 (thread-safe)
    # ────────────────────────────────────────────────────────────────────────

    def send_frame(self, main_evt: int, sub_evt: int, data: bytes = b""):
        """
        Đóng gói và gửi frame nhị phân xuống STM32 Master.

        Frame format (khớp pi_protocol.c phía STM32):
          0xAA | MAIN | SUB | LEN | DATA[0..LEN-1] | XOR_CHK | 0x55

        XOR_CHK = MAIN ^ SUB ^ LEN ^ DATA[0] ^ … ^ DATA[LEN-1]
        """
        length = len(data)
        frame  = bytearray([0xAA, main_evt, sub_evt, length])
        frame.extend(data)

        chk = main_evt ^ sub_evt ^ length
        for b in data:
            chk ^= b
        frame.append(chk)
        frame.append(0x55)

        with self.lock:
            if self.ser and self.ser.is_open:
                try:
                    self.ser.write(frame)
                    logger.debug(
                        "TX → Main=0x%02X Sub=0x%02X Len=%d  [%s]",
                        main_evt, sub_evt, length, frame.hex().upper()
                    )
                except serial.SerialException as e:
                    logger.error("UART write error: %s", e)
                    self._close_port()
            else:
                logger.warning(
                    "send_frame dropped (no connection): Main=0x%02X Sub=0x%02X",
                    main_evt, sub_evt
                )

    def play_event(self, event: tuple, data: bytes = b""):
        """F5: Gửi frame âm thanh theo tuple (main_evt, sub_evt)."""
        if isinstance(event, (tuple, list)) and len(event) >= 2:
            self.send_frame(event[0], event[1], data)

    # ────────────────────────────────────────────────────────────────────────
    # Thread chính
    # ────────────────────────────────────────────────────────────────────────

    def run(self):
        self.running   = True
        retry_delay    = 2    # giây, tăng dần nếu tiếp tục thất bại
        MAX_DELAY      = 30

        while self.running:
            # Thử (lại) mở cổng nếu chưa kết nối
            if not self.ser or not self.ser.is_open:
                if not self.open_port():
                    logger.info("Retry UART in %ds…", retry_delay)
                    time.sleep(retry_delay)
                    retry_delay = min(retry_delay * 2, MAX_DELAY)
                    continue
                retry_delay = 2   # reset khi kết nối thành công

            # Đọc & parse dữ liệu
            try:
                waiting = self.ser.in_waiting
                if waiting > 0:
                    raw = self.ser.read(waiting)
                    for byte in raw:
                        self.parse_byte(byte)
            except serial.SerialException as e:
                logger.error("UART read error: %s — reconnecting…", e)
                self._close_port()
                continue
            except Exception as e:
                logger.error("Unexpected UART error: %s", e)
                time.sleep(1)

            time.sleep(0.01)   # ~100 Hz polling

    # ────────────────────────────────────────────────────────────────────────
    # Parser frame nhị phân (state machine — khớp PiProtocol_ProcessByte)
    # ────────────────────────────────────────────────────────────────────────

    def parse_byte(self, b: int):
        if self.state == "STX":
            if b == 0xAA:
                self.state = "MAIN"

        elif self.state == "MAIN":
            self.rx_main = b
            self.state   = "SUB"

        elif self.state == "SUB":
            self.rx_sub = b
            self.state  = "LEN"

        elif self.state == "LEN":
            self.rx_len  = b
            self.rx_data = bytearray()
            if self.rx_len == 0:
                self.state = "CHECKSUM"
            elif self.rx_len <= 128:
                self.state = "DATA"
            else:
                logger.warning("Invalid frame LEN=0x%02X — reset", b)
                self.state = "STX"

        elif self.state == "DATA":
            self.rx_data.append(b)
            if len(self.rx_data) >= self.rx_len:
                self.state = "CHECKSUM"

        elif self.state == "CHECKSUM":
            self.rx_chk = b
            self.state  = "ETX"

        elif self.state == "ETX":
            if b == 0x55:
                chk = self.rx_main ^ self.rx_sub ^ self.rx_len
                for x in self.rx_data:
                    chk ^= x
                if chk == self.rx_chk:
                    self.process_received_frame(self.rx_main, self.rx_sub, self.rx_data)
                else:
                    logger.warning(
                        "Checksum mismatch! Calc=0x%02X Recv=0x%02X (Main=0x%02X Sub=0x%02X)",
                        chk, self.rx_chk, self.rx_main, self.rx_sub
                    )
            else:
                logger.warning("Missing ETX (got 0x%02X) — reset", b)
            self.state = "STX"

    # ────────────────────────────────────────────────────────────────────────
    # Dispatch frame đã được xác thực → emit Qt signals
    # ────────────────────────────────────────────────────────────────────────

    def process_received_frame(self, main_evt: int, sub_evt: int, data: bytearray):
        logger.debug(
            "RX ← Main=0x%02X Sub=0x%02X Len=%d  [%s]",
            main_evt, sub_evt, len(data), data.hex().upper()
        )

        # ── 0xF0 : GPS Telemetry — "lat,lon,speed_kmh" (ASCII) ───────────
        if main_evt == 0xF0:
            try:
                gps_str = data.decode("ascii").strip()
                lat, lon, speed = map(float, gps_str.split(","))
                self.gps_received.emit(lat, lon, speed)
            except Exception as e:
                logger.error("GPS parse error: %s  raw=%s", e, data)
                self.gps_received.emit(
                    config.SCHOOL_GEOFENCE_LAT, config.SCHOOL_GEOFENCE_LON, 0.0
                )

        # ── 0xF1 : Seat Status — "s1:0s2:1…s16:0" (ASCII) ───────────────
        elif main_evt == 0xF1:
            try:
                status_str = data.decode("ascii")
                matches = re.findall(r"s(\d+):([01])", status_str)
                seats   = {seat: int(val) for seat, val in matches}
                if len(seats) == 16:
                    self.seats_received.emit(seats)
                else:
                    logger.warning("Seat frame incomplete: %d/16 seats parsed", len(seats))
            except Exception as e:
                logger.error("Seat parse error: %s  raw=%s", e, data)

        # ── 0xF2 : RFID Card ID (ASCII) ───────────────────────────────────
        elif main_evt == 0xF2:
            try:
                card_str = data.decode("ascii").strip()
                self.rfid_received.emit(card_str)
            except Exception as e:
                logger.error("RFID decode error: %s", e)

        # ── 0xF3 : SOS Frame (Trigger / Cancel) ──────────────────────────
        elif main_evt == 0xF3:
            source = data[0] if len(data) > 0 else 1
            if sub_evt == 0x02:
                logger.info("UART RX 0xF3: SOS Cancel (source=%d)", source)
                self.sos_cancel_received.emit()
            else:
                logger.warning("UART RX 0xF3: SOS Trigger (source=%d)", source)
                self.sos_received.emit(source)

        # ── 0xF5 : DHT11 — "temperature,humidity" (ASCII) ────────────────
        elif main_evt == 0xF5:
            try:
                dht_str = data.decode("ascii").strip()
                temp, humid = map(float, dht_str.split(","))
                self.dht11_received.emit(temp, humid)
            except Exception as e:
                logger.error("DHT11 parse error: %s  raw=%s", e, data)

        else:
            logger.warning("Unknown main_evt=0x%02X sub=0x%02X", main_evt, sub_evt)

    # ────────────────────────────────────────────────────────────────────────
    # Dừng thread
    # ────────────────────────────────────────────────────────────────────────

    def stop(self):
        self.running = False
        self._close_port()
        self.wait()
