"""
seat_state.py — F2: Khi ghế chuyển 0→1 sau debounce, gọi boarding_logic._try_finalize_boarding
"""
import time
from services.logger import get_logger

logger = get_logger("SeatDebouncer")


class SeatDebouncer:
    def __init__(self, on_seat_occupied=None):
        """
        on_seat_occupied: callable(seat_number: int) — gọi khi ghế vừa chuyển 0→1
                          Dùng để trigger F2 finalize boarding.
        """
        self.current_states = {str(i): 0 for i in range(1, 17)}
        self.target_states = {str(i): 0 for i in range(1, 17)}
        self.change_timestamps = {str(i): 0.0 for i in range(1, 17)}
        self.debounce_delay = 10.0  # 10 giây

        # F2: callback khi ghế học sinh (3-16) vừa có người
        self._on_seat_occupied = on_seat_occupied

    def update_raw_seats(self, raw_seats: dict) -> tuple:
        """
        Nhận dữ liệu ghế thô từ UART.
        Trả về (changed_boolean, debounced_seats_dict).
        Khi ghế học sinh (3-16) chuyển 0→1 và debounce passed → gọi on_seat_occupied.
        """
        now = time.time()
        changed = False

        for i in range(1, 17):
            seat_num = str(i)
            raw_val = raw_seats.get(seat_num, 0)

            if raw_val != self.target_states[seat_num]:
                self.target_states[seat_num] = raw_val
                self.change_timestamps[seat_num] = now
            elif raw_val != self.current_states[seat_num]:
                if now - self.change_timestamps[seat_num] >= self.debounce_delay:
                    prev_state = self.current_states[seat_num]
                    self.current_states[seat_num] = raw_val
                    changed = True
                    logger.info("Seat %s state settled to %d", seat_num, raw_val)

                    # F2: ghế học sinh (3-16) vừa chuyển 0→1
                    if i >= 3 and prev_state == 0 and raw_val == 1:
                        if self._on_seat_occupied:
                            try:
                                self._on_seat_occupied(i)
                            except Exception as e:
                                logger.error("on_seat_occupied callback error (seat %d): %s", i, e)

        return changed, self.current_states.copy()

    def update(self, raw_seats: dict):
        """
        Wrapper tương thích cho UI app:
        Trả về dict trạng thái ghế khi có thay đổi ổn định (settled), hoặc None nếu không thay đổi.
        """
        changed, debounced = self.update_raw_seats(raw_seats)
        return debounced if changed else None
