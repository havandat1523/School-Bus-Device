#include "audio_queue.h"
#include <string.h>

static AudioItem_t s_queue[AUDIO_QUEUE_MAX_ITEMS];
static uint8_t s_count = 0;
static osMutexId_t s_mutex = NULL;
static volatile uint8_t s_loop_0801 = 0;

void AudioQueue_Init(void) {
    const osMutexAttr_t mutex_attr = {
        .name = "AudioQueueMutex",
        .attr_bits = osMutexPrioInherit
    };
    if (s_mutex == NULL) {
        s_mutex = osMutexNew(&mutex_attr);
    }
    s_count = 0;
    s_loop_0801 = 0;
    memset(s_queue, 0, sizeof(s_queue));
}

uint8_t AudioQueue_GetPriority(uint8_t main_evt, uint8_t sub_evt) {
    // Priority 4 (Cao nhất): Track 08/001 (Bỏ quên học sinh)
    if (main_evt == 0x08 && sub_evt == 0x01) {
        return AUDIO_PRIO_CRITICAL;
    }
    // Priority 3: Series 07/xxx (SOS khẩn cấp)
    if (main_evt == 0x07) {
        return AUDIO_PRIO_HIGH;
    }
    // Priority 2: Series 05/xxx (RFID học sinh), 06/xxx (Phụ xe, cảnh báo học sinh)
    if (main_evt == 0x05 || main_evt == 0x06) {
        return AUDIO_PRIO_NORMAL;
    }
    // Priority 1 (Thấp nhất): Login / Logout (01/xxx, 02/xxx, 03/xxx, 04/xxx)
    return AUDIO_PRIO_LOWEST;
}

uint8_t AudioQueue_Push(uint8_t main_evt, uint8_t sub_evt) {
    uint8_t prio = AudioQueue_GetPriority(main_evt, sub_evt);
    AudioItem_t new_item;
    new_item.main_evt = main_evt;
    new_item.sub_evt = sub_evt;
    new_item.priority = prio;
    new_item.duration_ms = (main_evt == 0x07 || (main_evt == 0x08 && sub_evt == 0x01)) ? 4000 : DEFAULT_TRACK_DURATION_MS;

    if (s_mutex != NULL) {
        osMutexAcquire(s_mutex, osWaitForever);
    }

    if (s_count >= AUDIO_QUEUE_MAX_ITEMS) {
        // Nếu hàng đợi đầy, kiểm tra xem item mới có priority cao hơn item cuối không
        if (prio > s_queue[s_count - 1].priority) {
            // Thay thế item cuối cùng có priority thấp hơn
            s_count--;
        } else {
            // Drop item mới vì priority không đủ cao và queue đã đầy
            if (s_mutex != NULL) osMutexRelease(s_mutex);
            return 0;
        }
    }

    // Tìm vị trí chèn để bảo toàn thứ tự: Priority cao đứng trước, cùng priority FIFO
    int insert_idx = s_count;
    for (int i = 0; i < s_count; i++) {
        if (prio > s_queue[i].priority) {
            insert_idx = i;
            break;
        }
    }

    // Dịch các phần tử phía sau sang phải
    for (int i = s_count; i > insert_idx; i--) {
        s_queue[i] = s_queue[i - 1];
    }

    s_queue[insert_idx] = new_item;
    s_count++;

    if (s_mutex != NULL) {
        osMutexRelease(s_mutex);
    }
    return 1;
}

uint8_t AudioQueue_Pop(AudioItem_t *item) {
    if (s_mutex != NULL) {
        osMutexAcquire(s_mutex, osWaitForever);
    }

    if (s_count == 0) {
        if (s_mutex != NULL) osMutexRelease(s_mutex);
        return 0;
    }

    if (item != NULL) {
        *item = s_queue[0];
    }

    // Dịch toàn bộ mảng lên 1 vị trí
    for (int i = 0; i < s_count - 1; i++) {
        s_queue[i] = s_queue[i + 1];
    }
    s_count--;

    if (s_mutex != NULL) {
        osMutexRelease(s_mutex);
    }
    return 1;
}

uint8_t AudioQueue_PeekPriority(void) {
    uint8_t prio = 0;
    if (s_mutex != NULL) {
        osMutexAcquire(s_mutex, osWaitForever);
    }
    if (s_count > 0) {
        prio = s_queue[0].priority;
    }
    if (s_mutex != NULL) {
        osMutexRelease(s_mutex);
    }
    return prio;
}

uint8_t AudioQueue_IsEmpty(void) {
    uint8_t empty = 1;
    if (s_mutex != NULL) {
        osMutexAcquire(s_mutex, osWaitForever);
    }
    empty = (s_count == 0) ? 1 : 0;
    if (s_mutex != NULL) {
        osMutexRelease(s_mutex);
    }
    return empty;
}

void AudioQueue_Clear(void) {
    if (s_mutex != NULL) {
        osMutexAcquire(s_mutex, osWaitForever);
    }
    s_count = 0;
    memset(s_queue, 0, sizeof(s_queue));
    if (s_mutex != NULL) {
        osMutexRelease(s_mutex);
    }
}

void AudioQueue_SetLoop0801(uint8_t enable) {
    s_loop_0801 = enable;
}

uint8_t AudioQueue_IsLoop0801(void) {
    return s_loop_0801;
}
