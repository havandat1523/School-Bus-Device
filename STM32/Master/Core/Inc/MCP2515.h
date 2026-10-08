#ifndef INC_MCP2515_H_
#define INC_MCP2515_H_

#include "stm32f4xx_hal.h"

// MCP2515 Crystal Oscillator Frequency: 8MHz or 16MHz (Default 8MHz)
#define MCP2515_CRYSTAL_8MHZ
// #define MCP2515_CRYSTAL_16MHZ

// SPI Instruction Set
#define MCP_RESET           0xC0
#define MCP_READ            0x03
#define MCP_WRITE           0x02
#define MCP_RTS             0x80
#define MCP_RTS_TX0         0x81
#define MCP_RTS_TX1         0x82
#define MCP_RTS_TX2         0x84
#define MCP_RTS_ALL         0x87
#define MCP_READ_STATUS     0xA0
#define MCP_RX_STATUS       0xB0
#define MCP_BITMOD          0x05

// Register addresses
#define MCP_RXF0SIDH        0x00
#define MCP_RXF0SIDL        0x01
#define MCP_CANSTAT         0x0E
#define MCP_CANCTRL         0x0F
#define MCP_TEC             0x1C
#define MCP_REC             0x1D

#define MCP_CNF3            0x28
#define MCP_CNF2            0x29
#define MCP_CNF1            0x2A

#define MCP_CANINTE         0x2B
#define MCP_CANINTF         0x2C

#define MCP_TXB0CTRL        0x30
#define MCP_TXB0SIDH        0x31
#define MCP_TXB0SIDL        0x32
#define MCP_TXB0DLC         0x35
#define MCP_TXB0D0          0x36

#define MCP_RXB0CTRL        0x60
#define MCP_RXB0SIDH        0x61
#define MCP_RXB0SIDL        0x62
#define MCP_RXB0DLC         0x65
#define MCP_RXB0D0          0x66

#define MCP_RXB1CTRL        0x70
#define MCP_RXB1SIDH        0x71
#define MCP_RXB1SIDL        0x72
#define MCP_RXB1DLC         0x75
#define MCP_RXB1D0          0x76

// Configuration bitmasks
#define MODE_NORMAL         0x00
#define MODE_SLEEP          0x20
#define MODE_LOOPBACK       0x40
#define MODE_LISTENONLY     0x60
#define MODE_CONFIG         0x80

#define MODE_MASK           0xE0
#define ABTF_MASK           0x10
#define OSM_MASK            0x08
#define CLKEN_MASK          0x04
#define CLKPRE_MASK         0x03

// CAN Frame struct
typedef struct {
    uint32_t id;
    uint8_t dlc;
    uint8_t data[8];
} CAN_Message_t;

// Driver functions
uint8_t MCP2515_Init(SPI_HandleTypeDef *hspi, GPIO_TypeDef *cs_port, uint16_t cs_pin);
void MCP2515_Reset(void);
void MCP2515_WriteReg(uint8_t reg, uint8_t val);
uint8_t MCP2515_ReadReg(uint8_t reg);
void MCP2515_BitModify(uint8_t reg, uint8_t mask, uint8_t val);
uint8_t MCP2515_SendCANMessage(CAN_Message_t *msg);
uint8_t MCP2515_ReceiveCANMessage(CAN_Message_t *msg);
uint8_t MCP2515_CheckReceive(void);

#endif /* INC_MCP2515_H_ */
