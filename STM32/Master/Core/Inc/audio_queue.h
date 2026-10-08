#ifndef INC_AUDIO_QUEUE_H_
#define INC_AUDIO_QUEUE_H_

#include "stm32f4xx_hal.h"
#include "cmsis_os2.h"

#ifdef __cplusplus
extern "C" {
#endif

#define AUDIO_QUEUE_MAX_ITEMS 16
#define DEFAULT_TRACK_DURATION_MS 3500  // Mặc định mỗi âm thanh ~3.5 giây

/**
 * @brief Định nghĩa mức độ ưu tiên âm thanh:
 * Priority 4 (Cao nhất): 08/001 (Bỏ quên học sinh - lặp liên tục tới khi hủy)
 * Priority 3           : 07/001 -> 07/005 (SOS Series khẩn cấp - ngắt track thấp)
 * Priority 2           : 05/xxx, 06/xxx (Học sinh, phụ xe)
 * Priority 1 (Thấp nhất): 01/xxx, 02/xxx, 03/xxx, 04/xxx (Login / Logout)
 */
#define AUDIO_PRIO_LOWEST   1
#define AUDIO_PRIO_NORMAL   2
#define AUDIO_PRIO_HIGH     3
#define AUDIO_PRIO_CRITICAL 4

typedef struct {
    uint8_t main_evt;
    uint8_t sub_evt;
    uint8_t priority;
    uint32_t duration_ms;
} AudioItem_t;

/**
 * @brief Khởi tạo hàng đợi âm thanh và mutex bảo vệ
 */
void AudioQueue_Init(void);

/**
 * @brief Xác định mức độ ưu tiên dựa trên mã thư mục và track
 */
uint8_t AudioQueue_GetPriority(uint8_t main_evt, uint8_t sub_evt);

/**
 * @brief Thêm âm thanh vào hàng đợi ưu tiên (Priority Queue)
 * @retval 1 nếu thêm thành công, 0 nếu hàng đợi đầy
 */
uint8_t AudioQueue_Push(uint8_t main_evt, uint8_t sub_evt);

/**
 * @brief Lấy âm thanh có độ ưu tiên cao nhất ra khỏi hàng đợi
 * @retval 1 nếu lấy được, 0 nếu hàng đợi trống
 */
uint8_t AudioQueue_Pop(AudioItem_t *item);

/**
 * @brief Lấy độ ưu tiên cao nhất đang chờ trong hàng đợi (không lấy ra khỏi queue)
 * @retval 0 nếu trống, >0 là priority
 */
uint8_t AudioQueue_PeekPriority(void);

/**
 * @brief Kiểm tra hàng đợi có rỗng không
 */
uint8_t AudioQueue_IsEmpty(void);

/**
 * @brief Xóa toàn bộ âm thanh trong hàng đợi
 */
void AudioQueue_Clear(void);

/**
 * @brief Quản lý cờ lặp lại của track 08/001 (Critical Alarm)
 */
void AudioQueue_SetLoop0801(uint8_t enable);
uint8_t AudioQueue_IsLoop0801(void);

#ifdef __cplusplus
}
#endif

#endif /* INC_AUDIO_QUEUE_H_ */
