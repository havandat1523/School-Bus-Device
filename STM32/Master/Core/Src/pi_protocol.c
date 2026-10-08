#include "pi_protocol.h"
#include <string.h>

static UART_HandleTypeDef *p_pi_uart = NULL;
static PiRxState_t rx_state = PI_RX_STX;
static PiFrame_t rx_temp_frame;
static uint8_t rx_data_idx = 0;
static uint8_t rx_expected_chk = 0;

void PiProtocol_Init(UART_HandleTypeDef *huart) {
    p_pi_uart = huart;
    rx_state = PI_RX_STX;
}

uint8_t PiProtocol_SendFrame(uint8_t main_evt, uint8_t sub_evt, uint8_t len, const uint8_t *data) {
    if (!p_pi_uart) return 0;

    uint8_t tx_buf[150];
    tx_buf[0] = PI_FRAME_STX;
    tx_buf[1] = main_evt;
    tx_buf[2] = sub_evt;
    tx_buf[3] = len;

    uint8_t chk = main_evt ^ sub_evt ^ len;
    for (uint8_t i = 0; i < len; i++) {
        tx_buf[4 + i] = data[i];
        chk ^= data[i];
    }

    tx_buf[4 + len] = chk;
    tx_buf[5 + len] = PI_FRAME_ETX;

    HAL_StatusTypeDef status = HAL_UART_Transmit(p_pi_uart, tx_buf, 6 + len, 500);
    return (status == HAL_OK) ? 1 : 0;
}

uint8_t PiProtocol_ProcessByte(uint8_t byte, PiFrame_t *out_frame) {
    uint8_t frame_ready = 0;

    switch (rx_state) {
        case PI_RX_STX:
            if (byte == PI_FRAME_STX) {
                rx_state = PI_RX_MAIN;
            }
            break;

        case PI_RX_MAIN:
            rx_temp_frame.main_evt = byte;
            rx_state = PI_RX_SUB;
            break;

        case PI_RX_SUB:
            rx_temp_frame.sub_evt = byte;
            rx_state = PI_RX_LEN;
            break;

        case PI_RX_LEN:
            rx_temp_frame.len = byte;
            rx_data_idx = 0;
            if (rx_temp_frame.len > 0 && rx_temp_frame.len <= 128) {
                rx_state = PI_RX_DATA;
            } else if (rx_temp_frame.len == 0) {
                rx_state = PI_RX_CHECKSUM;
            } else {
                rx_state = PI_RX_STX; // Invalid len
            }
            break;

        case PI_RX_DATA:
            rx_temp_frame.data[rx_data_idx++] = byte;
            if (rx_data_idx >= rx_temp_frame.len) {
                rx_state = PI_RX_CHECKSUM;
            }
            break;

        case PI_RX_CHECKSUM:
            rx_expected_chk = byte;
            rx_state = PI_RX_ETX;
            break;

        case PI_RX_ETX:
            if (byte == PI_FRAME_ETX) {
                // Verify XOR checksum
                uint8_t calc_chk = rx_temp_frame.main_evt ^ rx_temp_frame.sub_evt ^ rx_temp_frame.len;
                for (uint8_t i = 0; i < rx_temp_frame.len; i++) {
                    calc_chk ^= rx_temp_frame.data[i];
                }
                if (calc_chk == rx_expected_chk) {
                    if (out_frame) {
                        memcpy(out_frame, &rx_temp_frame, sizeof(PiFrame_t));
                    }
                    frame_ready = 1;
                }
            }
            rx_state = PI_RX_STX;
            break;

        default:
            rx_state = PI_RX_STX;
            break;
    }

    return frame_ready;
}
