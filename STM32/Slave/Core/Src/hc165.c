#include "hc165.h"
#include "FreeRTOS.h"
#include "task.h"

static uint8_t current_states[16] = {0}; // 0 = empty, 1 = occupied
static uint8_t target_states[16] = {0};
static TickType_t state_change_time[16] = {0};
static uint16_t debounced_bitmap = 0;

void HC165_Init(void) {
    HAL_GPIO_WritePin(HC165_PL_GPIO_Port, HC165_PL_Pin, GPIO_PIN_SET);
    HAL_GPIO_WritePin(HC165_CLK_GPIO_Port, HC165_CLK_Pin, GPIO_PIN_RESET);

    // Initial read
    uint16_t initial_raw = HC165_ReadRaw16();
    TickType_t now = xTaskGetTickCount();

    debounced_bitmap = 0;
    for (int i = 0; i < 16; i++) {
        uint8_t reading = (initial_raw >> (15 - i)) & 0x01;
        current_states[i] = reading;
        target_states[i] = reading;
        state_change_time[i] = now;
        if (reading) {
            debounced_bitmap |= (1 << i);
        }
    }
}

uint16_t HC165_ReadRaw16(void) {
    uint16_t data = 0;

    // 1. Parallel Load pulse: LOW then HIGH
    HAL_GPIO_WritePin(HC165_PL_GPIO_Port, HC165_PL_Pin, GPIO_PIN_RESET);
    for (volatile int i = 0; i < 10; i++);
    HAL_GPIO_WritePin(HC165_PL_GPIO_Port, HC165_PL_Pin, GPIO_PIN_SET);

    // 2. Shift out 16 bits
    for (int i = 0; i < 16; i++) {
        // Seat switch pulls input to GND when pressed (active LOW)
        // 0 = Occupied -> inverted to 1
        // 1 = Empty -> inverted to 0
        uint8_t bit_val = (HAL_GPIO_ReadPin(HC165_DATA_GPIO_Port, HC165_DATA_Pin) == GPIO_PIN_RESET) ? 1 : 0;
        data |= (bit_val << (15 - i));

        // Clock pulse: HIGH then LOW
        HAL_GPIO_WritePin(HC165_CLK_GPIO_Port, HC165_CLK_Pin, GPIO_PIN_SET);
        for (volatile int i = 0; i < 10; i++);
        HAL_GPIO_WritePin(HC165_CLK_GPIO_Port, HC165_CLK_Pin, GPIO_PIN_RESET);
    }

    return data;
}

uint8_t HC165_UpdateDebounce(uint16_t *out_debounced_bitmap) {
    uint16_t raw = HC165_ReadRaw16();
    TickType_t now = xTaskGetTickCount();
    uint8_t changed = 0;

    for (int i = 0; i < 16; i++) {
        uint8_t reading = (raw >> (15 - i)) & 0x01;

        if (reading != target_states[i]) {
            target_states[i] = reading;
            state_change_time[i] = now;
        } else {
            if (reading != current_states[i]) {
                if ((now - state_change_time[i]) >= pdMS_TO_TICKS(DEBOUNCE_DELAY_MS)) {
                    current_states[i] = reading;
                    changed = 1;
                    if (reading) {
                        debounced_bitmap |= (1 << i);
                    } else {
                        debounced_bitmap &= ~(1 << i);
                    }
                }
            }
        }
    }

    if (out_debounced_bitmap) {
        *out_debounced_bitmap = debounced_bitmap;
    }

    return changed;
}
