"""
sim_manager.py — Quản lý cuộc gọi khẩn cấp khi kích hoạt SOS Slave
"""
import time
import threading
from typing import Optional
from config import config
from services.logger import get_logger
from apps.sim_manager.at_commands import ATCommandInterface

logger = get_logger("SimManager")


class SimManager:
    def __init__(
        self,
        uart_port: Optional[str] = None,
        baudrate: Optional[int] = None,
        default_phone: Optional[str] = None,
        call_interval_sec: Optional[int] = None,
        call_max: Optional[int] = None
    ):
        self.uart_port = uart_port or getattr(config, "SIM_UART_PORT", "/dev/ttyAMA1")
        self.baudrate = baudrate or getattr(config, "SIM_UART_BAUDRATE", 115200)
        self.default_phone = default_phone or getattr(config, "DEFAULT_PHONE_NUMBER", "0987654321")
        self.call_interval_sec = call_interval_sec or getattr(config, "SOS_CALL_INTERVAL_SEC", 120)
        self.call_max = call_max or getattr(config, "SOS_CALL_MAX", 4)

        self.at = ATCommandInterface(port=self.uart_port, baudrate=self.baudrate)
        self._thread: Optional[threading.Thread] = None
        self._stop_event = threading.Event()
        self._is_calling = False

    def start_call_loop(self, target_phone: Optional[str] = None):
        """
        Khởi động luồng gọi điện thoại khẩn cấp.
        Chỉ kích hoạt khi SOS từ Slave (F3 requirement).
        """
        if self._thread and self._thread.is_alive():
            logger.warning("SimManager call loop is already active!")
            return

        phone_to_call = target_phone.strip() if target_phone and target_phone.strip() else self.default_phone
        logger.warning("🚨 SimManager starting emergency call loop to: %s (Max %d calls, interval %ds)",
                       phone_to_call, self.call_max, self.call_interval_sec)

        self._stop_event.clear()
        self._thread = threading.Thread(
            target=self._call_loop_worker,
            args=(phone_to_call,),
            name="SimManager-CallLoop",
            daemon=True
        )
        self._thread.start()

    def _call_loop_worker(self, phone: str):
        self._is_calling = True
        try:
            self.at.init_modem()
        except Exception as e:
            logger.error("Failed to init SIM modem: %s", e)

        for attempt in range(1, self.call_max + 1):
            if self._stop_event.is_set():
                logger.info("SimManager call loop stopped before attempt %d", attempt)
                break

            logger.info("📞 SimManager call attempt %d/%d to %s", attempt, self.call_max, phone)
            try:
                self.at.call_voice(phone)
            except Exception as e:
                logger.error("Error making voice call: %s", e)

            # Để đổ chuông tối đa 35 giây (hoặc cho đến khi bị huỷ)
            ring_start = time.time()
            while time.time() - ring_start < 35:
                if self._stop_event.is_set():
                    logger.info("SimManager: Cancel event received while ringing — hanging up!")
                    self.at.hangup()
                    self._is_calling = False
                    return
                time.sleep(0.5)

            # Ngắt chuông sau 35s
            try:
                self.at.hangup()
            except Exception as e:
                logger.error("Error hanging up: %s", e)

            # Chờ khoảng thời gian còn lại (120s - 35s = 85s) trước lần gọi tiếp theo
            if attempt < self.call_max:
                wait_sec = max(5, self.call_interval_sec - 35)
                logger.info("SimManager: Waiting %ds before next call attempt...", wait_sec)
                wait_start = time.time()
                while time.time() - wait_start < wait_sec:
                    if self._stop_event.is_set():
                        logger.info("SimManager: Cancel event received while waiting between calls")
                        self._is_calling = False
                        return
                    time.sleep(0.5)

        logger.info("SimManager: Finished emergency call loop (%d attempts completed)", self.call_max)
        self._is_calling = False

    def stop(self):
        """Dừng khẩn cấp cuộc gọi ngay lập tức."""
        logger.info("SimManager stop requested")
        self._stop_event.set()
        try:
            self.at.hangup()
        except Exception:
            pass
        self._is_calling = False

    @property
    def is_active(self) -> bool:
        return self._is_calling
