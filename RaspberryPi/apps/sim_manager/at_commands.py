"""
at_commands.py — Giao tiếp AT commands với module SIM A7680s qua UART
"""
import time
import serial
from services.logger import get_logger

logger = get_logger("ATCommands")


class ATCommandInterface:
    def __init__(self, port: str = "/dev/ttyAMA1", baudrate: int = 115200, timeout: float = 2.0):
        self.port = port
        self.baudrate = baudrate
        self.timeout = timeout
        self.ser = None

    def connect(self) -> bool:
        """Mở cổng Serial kết nối tới SIM A7680s."""
        try:
            self.ser = serial.Serial(
                port=self.port,
                baudrate=self.baudrate,
                bytesize=serial.EIGHTBITS,
                parity=serial.PARITY_NONE,
                stopbits=serial.STOPBITS_ONE,
                timeout=self.timeout
            )
            logger.info("SIM A7680s UART opened on %s @ %d", self.port, self.baudrate)
            return True
        except Exception as e:
            logger.warning("Could not open SIM UART %s: %s (running in fallback/mock mode)", self.port, e)
            self.ser = None
            return False

    def close(self):
        """Đóng cổng Serial."""
        if self.ser and self.ser.is_open:
            try:
                self.ser.close()
            except Exception:
                pass
        self.ser = None

    def send_cmd(self, cmd: str, wait_response: bool = True, timeout: float = 2.0) -> str:
        """Gửi 1 lệnh AT và nhận phản hồi."""
        if not self.ser or not self.ser.is_open:
            if not self.connect():
                logger.debug("Sim serial not available — simulated CMD: %s", cmd)
                return "OK"

        try:
            full_cmd = (cmd + "\r\n").encode("ascii")
            self.ser.write(full_cmd)
            logger.debug("SIM TX: %s", cmd)

            if not wait_response:
                return ""

            response_lines = []
            deadline = time.time() + timeout
            while time.time() < deadline:
                line = self.ser.readline().decode("ascii", errors="ignore").strip()
                if line:
                    response_lines.append(line)
                    if "OK" in line or "ERROR" in line or "NO CARRIER" in line or "BUSY" in line:
                        break
            resp = "\n".join(response_lines)
            logger.debug("SIM RX: %s", resp)
            return resp
        except Exception as e:
            logger.error("AT command error on '%s': %s", cmd, e)
            return "ERROR"

    def init_modem(self) -> bool:
        """Kiểm tra và khởi tạo module SIM."""
        resp = self.send_cmd("AT")
        if "OK" in resp:
            self.send_cmd("ATE0")  # Tắt echo
            return True
        return False

    def call_voice(self, phone_number: str) -> bool:
        """
        Gọi điện thoại bằng lệnh ATD{phone_number};
        LƯU Ý: Phải có dấu ';' ở cuối để là cuộc gọi thoại (Voice call), không phải Data call.
        """
        # Chuẩn hoá số điện thoại
        clean_phone = phone_number.strip().replace(" ", "")
        cmd = f"ATD{clean_phone};"
        logger.info("SIM initiating voice call: %s", cmd)
        resp = self.send_cmd(cmd, timeout=5.0)
        return "OK" in resp

    def hangup(self) -> bool:
        """Ngắt cuộc gọi (ATH)."""
        logger.info("SIM hanging up call (ATH)")
        resp = self.send_cmd("ATH", timeout=3.0)
        return "OK" in resp
