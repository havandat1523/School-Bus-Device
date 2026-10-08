#ifndef INC_PI_PROTOCOL_H_
#define INC_PI_PROTOCOL_H_

#include "stm32f4xx_hal.h"
#include <stdint.h>

#define PI_FRAME_STX 0xAA
#define PI_FRAME_ETX 0x55

typedef struct {
    uint8_t main_evt;
    uint8_t sub_evt;
    uint8_t len;
    uint8_t data[128];
} PiFrame_t;

typedef enum {
    PI_RX_STX,
    PI_RX_MAIN,
    PI_RX_SUB,
    PI_RX_LEN,
    PI_RX_DATA,
    PI_RX_CHECKSUM,
    PI_RX_ETX
} PiRxState_t;

void PiProtocol_Init(UART_HandleTypeDef *huart);
uint8_t PiProtocol_SendFrame(uint8_t main_evt, uint8_t sub_evt, uint8_t len, const uint8_t *data);
uint8_t PiProtocol_ProcessByte(uint8_t byte, PiFrame_t *out_frame);

#endif /* INC_PI_PROTOCOL_H_ */
