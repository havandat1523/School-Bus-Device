"""
gpio_handler.py — Quản lý 2 nút GPIO vật lý trên Raspberry Pi (F4)
  Nút 1 (GPIO 17): Login / Logout toggle
  Nút 2 (GPIO 27): SOS khẩn cấp (nhấn giữ 3 giây liên tục)
"""
import time
import threading
from typing import Optional, Callable
from services.logger import get_logger

logger = get_logger("GPIOHandler")

# Thử import RPi.GPIO, nếu không có (Windows / Dev PC) thì chuyển sang Dummy
try:
    import RPi.GPIO as GPIO
    GPIO_AVAILABLE = True
except (ImportError, RuntimeError):
    GPIO_AVAILABLE = False
    logger.warning("RPi.GPIO not available on this platform. Running GPIOHandler in simulation/mock mode.")


class GPIOHandler:
    def __init__(
        self,
        login_pin: int = 17,
        sos_pin: int = 27,
        sos_hold_sec: float = 3.0,
        on_login_toggle: Optional[Callable[[], None]] = None,
        on_sos_triggered: Optional[Callable[[int], None]] = None,
        on_sos_progress: Optional[Callable[[float], None]] = None
    ):
        self.login_pin = login_pin
        self.sos_pin = sos_pin
        self.sos_hold_sec = sos_hold_sec

        self.on_login_toggle = on_login_toggle
        self.on_sos_triggered = on_sos_triggered
        self.on_sos_progress = on_sos_progress

        self._running = False
        self._sos_press_time: Optional[float] = None
        self._sos_fired = False
        self._sos_monitor_thread: Optional[threading.Thread] = None
        self._last_login_time = 0.0

    def start(self):
        """Khởi tạo cấu hình các chân GPIO và gắn ngắt / polling."""
        self._running = True
        if not GPIO_AVAILABLE:
            logger.info("GPIOHandler started (MOCK mode — pins: Login=%d, SOS=%d, Hold=%.1fs)",
                        self.login_pin, self.sos_pin, self.sos_hold_sec)
            return

        try:
            GPIO.setmode(GPIO.BCM)
            GPIO.setwarnings(False)

            # Cấu hình PULL_UP (mặc định HIGH, nhấn nút nối GND là LOW)
            GPIO.setup(self.login_pin, GPIO.IN, pull_up_down=GPIO.PUD_UP)
            GPIO.setup(self.sos_pin, GPIO.IN, pull_up_down=GPIO.PUD_UP)

            # Nút 1: Ngắt sườn xuống FALLING
            GPIO.add_event_detect(
                self.login_pin,
                GPIO.FALLING,
                callback=self._handle_login_interrupt,
                bouncetime=300
            )

            # Nút 2: Khởi động thread giám sát nút SOS liên tục
            self._sos_monitor_thread = threading.Thread(
                target=self._sos_monitor_loop,
                name="GPIO-SOS-Monitor",
                daemon=True
            )
            self._sos_monitor_thread.start()

            logger.info("GPIOHandler started successfully (Pins: Login=%d, SOS=%d, Hold=%.1fs)",
                        self.login_pin, self.sos_pin, self.sos_hold_sec)

        except Exception as e:
            logger.error("Failed to initialize GPIO pins: %s", e)

    def stop(self):
        """Dừng và dọn dẹp tài nguyên GPIO."""
        self._running = False
        if GPIO_AVAILABLE:
            try:
                GPIO.cleanup([self.login_pin, self.sos_pin])
            except Exception:
                pass
        logger.info("GPIOHandler stopped")

    # ─────────────────────────────────────────────────────────────────────────
    # Nút 1: Login / Logout Toggle
    # ─────────────────────────────────────────────────────────────────────────

    def _handle_login_interrupt(self, channel):
        now = time.time()
        # Debounce phần mềm 300ms
        if now - self._last_login_time < 0.3:
            return
        self._last_login_time = now

        logger.info("GPIO Button 1 (Login/Logout) pressed on pin %d", channel)
        if self.on_login_toggle:
            try:
                self.on_login_toggle()
            except Exception as e:
                logger.error("Error in on_login_toggle callback: %s", e)

    # ─────────────────────────────────────────────────────────────────────────
    # Nút 2: SOS Hold 3 Seconds
    # ─────────────────────────────────────────────────────────────────────────

    def _sos_monitor_loop(self):
        """
        Vòng lặp quét chân SOS pin mỗi 50ms:
        - Nút nhấn giữ LOW đủ 3 giây liên tục mới trigger SOS (triggered_by=3).
        - Gửi tiến độ (0.0 -> 1.0) để UI hiển thị progress bar / countdown.
        - Nhả ra trước 3s -> hủy và reset tiến độ về 0.
        """
        while self._running:
            try:
                pin_val = GPIO.input(self.sos_pin)
            except Exception:
                time.sleep(0.1)
                continue

            now = time.time()

            if pin_val == GPIO.LOW:  # Đang nhấn giữ nút
                if self._sos_press_time is None:
                    # Bắt đầu nhấn
                    self._sos_press_time = now
                    self._sos_fired = False
                    logger.debug("SOS GPIO pin %d pressed down — starting 3s timer", self.sos_pin)

                elapsed = now - self._sos_press_time
                progress = min(1.0, elapsed / self.sos_hold_sec)

                if self.on_sos_progress:
                    try:
                        self.on_sos_progress(progress)
                    except Exception:
                        pass

                if elapsed >= self.sos_hold_sec and not self._sos_fired:
                    self._sos_fired = True
                    logger.warning("🚨 GPIO SOS HOLD DETECTED (>= %.1fs) on pin %d! Triggering SOS (source=3)",
                                   self.sos_hold_sec, self.sos_pin)
                    if self.on_sos_triggered:
                        try:
                            self.on_sos_triggered(3)  # triggered_by = 3 (gpio_button)
                        except Exception as e:
                            logger.error("Error in on_sos_triggered callback: %s", e)

            else:  # Nút đã nhả ra (HIGH)
                if self._sos_press_time is not None:
                    elapsed = now - self._sos_press_time
                    if elapsed < self.sos_hold_sec:
                        logger.info("SOS button released after %.2fs (< %.1fs) — CANCELLED",
                                    elapsed, self.sos_hold_sec)
                    self._sos_press_time = None
                    self._sos_fired = False
                    if self.on_sos_progress:
                        try:
                            self.on_sos_progress(0.0)
                        except Exception:
                            pass

            time.sleep(0.05)  # 20 Hz polling rate

    # Phương thức mô phỏng cho testing
    def simulate_login_press(self):
        """Dùng cho kiểm thử mô phỏng trên PC."""
        self._handle_login_interrupt(self.login_pin)

    def simulate_sos_hold(self, duration_sec: float = 3.0):
        """Dùng cho kiểm thử mô phỏng trên PC."""
        logger.info("Simulating SOS hold for %.1fs...", duration_sec)
        steps = int(duration_sec / 0.1)
        for i in range(steps):
            prog = min(1.0, (i * 0.1) / self.sos_hold_sec)
            if self.on_sos_progress:
                self.on_sos_progress(prog)
            time.sleep(0.1)

        if duration_sec >= self.sos_hold_sec:
            if self.on_sos_progress:
                self.on_sos_progress(1.0)
            if self.on_sos_triggered:
                self.on_sos_triggered(3)
        else:
            if self.on_sos_progress:
                self.on_sos_progress(0.0)
