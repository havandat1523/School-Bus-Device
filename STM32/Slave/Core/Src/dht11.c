#include "dht11.h"
#include "FreeRTOS.h"
#include "task.h"

static void delay_us(uint32_t us) {
    __IO uint32_t count = us * (SystemCoreClock / 8000000);
    do {
        __NOP();
    } while (count--);
}

static void Set_Pin_Output(void) {
    GPIO_InitTypeDef GPIO_InitStruct = {0};
    GPIO_InitStruct.Pin = DHT11_PIN_Pin;
    GPIO_InitStruct.Mode = GPIO_MODE_OUTPUT_PP;
    GPIO_InitStruct.Pull = GPIO_PULLUP;
    GPIO_InitStruct.Speed = GPIO_SPEED_FREQ_LOW;
    HAL_GPIO_Init(DHT11_PIN_GPIO_Port, &GPIO_InitStruct);
}

static void Set_Pin_Input(void) {
    GPIO_InitTypeDef GPIO_InitStruct = {0};
    GPIO_InitStruct.Pin = DHT11_PIN_Pin;
    GPIO_InitStruct.Mode = GPIO_MODE_INPUT;
    GPIO_InitStruct.Pull = GPIO_PULLUP;
    HAL_GPIO_Init(DHT11_PIN_GPIO_Port, &GPIO_InitStruct);
}

void DHT11_Init(void) {
    Set_Pin_Output();
    HAL_GPIO_WritePin(DHT11_PIN_GPIO_Port, DHT11_PIN_Pin, GPIO_PIN_SET);
}

uint8_t DHT11_Read(DHT11_Data_t *out_data) {
    uint8_t data[5] = {0};
    uint16_t timeout = 0;

    // 1. Send Start Signal: Pull down for 18ms
    Set_Pin_Output();
    HAL_GPIO_WritePin(DHT11_PIN_GPIO_Port, DHT11_PIN_Pin, GPIO_PIN_RESET);
    HAL_Delay(18);
    HAL_GPIO_WritePin(DHT11_PIN_GPIO_Port, DHT11_PIN_Pin, GPIO_PIN_SET);
    delay_us(30);

    // 2. Switch to Input and Wait for Response
    Set_Pin_Input();

    // Critical section protects 1-wire timing from RTOS context switches
    taskENTER_CRITICAL();

    timeout = 0;
    while (HAL_GPIO_ReadPin(DHT11_PIN_GPIO_Port, DHT11_PIN_Pin) == GPIO_PIN_SET) {
        delay_us(1);
        if (timeout++ > 100) {
            taskEXIT_CRITICAL();
            return 0;
        }
    }

    timeout = 0;
    while (HAL_GPIO_ReadPin(DHT11_PIN_GPIO_Port, DHT11_PIN_Pin) == GPIO_PIN_RESET) {
        delay_us(1);
        if (timeout++ > 100) {
            taskEXIT_CRITICAL();
            return 0;
        }
    }

    timeout = 0;
    while (HAL_GPIO_ReadPin(DHT11_PIN_GPIO_Port, DHT11_PIN_Pin) == GPIO_PIN_SET) {
        delay_us(1);
        if (timeout++ > 100) {
            taskEXIT_CRITICAL();
            return 0;
        }
    }

    // 3. Read 40 bits
    for (int i = 0; i < 40; i++) {
        timeout = 0;
        while (HAL_GPIO_ReadPin(DHT11_PIN_GPIO_Port, DHT11_PIN_Pin) == GPIO_PIN_RESET) {
            delay_us(1);
            if (timeout++ > 100) {
                taskEXIT_CRITICAL();
                return 0;
            }
        }

        delay_us(40); // 26-28us = 0, 70us = 1

        if (HAL_GPIO_ReadPin(DHT11_PIN_GPIO_Port, DHT11_PIN_Pin) == GPIO_PIN_SET) {
            data[i / 8] |= (1 << (7 - (i % 8)));

            timeout = 0;
            while (HAL_GPIO_ReadPin(DHT11_PIN_GPIO_Port, DHT11_PIN_Pin) == GPIO_PIN_SET) {
                delay_us(1);
                if (timeout++ > 100) {
                    taskEXIT_CRITICAL();
                    return 0;
                }
            }
        }
    }

    taskEXIT_CRITICAL();

    // 4. Verify Checksum
    if ((data[0] + data[1] + data[2] + data[3]) == data[4]) {
        if (out_data) {
            out_data->hum_int  = data[0];
            out_data->hum_dec  = data[1];
            out_data->temp_int = data[2];
            out_data->temp_dec = data[3];
        }
        return 1;
    }

    return 0;
}
