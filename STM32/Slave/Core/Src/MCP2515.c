#include "MCP2515.h"

static SPI_HandleTypeDef *p_hspi;
static GPIO_TypeDef *p_cs_port;
static uint16_t p_cs_pin;

static void CS_Low(void) {
    HAL_GPIO_WritePin(p_cs_port, p_cs_pin, GPIO_PIN_RESET);
}

static void CS_High(void) {
    HAL_GPIO_WritePin(p_cs_port, p_cs_pin, GPIO_PIN_SET);
}

static uint8_t SPI_TxRx(uint8_t data) {
    uint8_t rx_data = 0;
    HAL_SPI_TransmitReceive(p_hspi, &data, &rx_data, 1, 10);
    return rx_data;
}

void MCP2515_Reset(void) {
    CS_Low();
    SPI_TxRx(MCP_RESET);
    CS_High();
    HAL_Delay(10); // Wait for chip to restart
}

uint8_t MCP2515_ReadReg(uint8_t reg) {
    uint8_t val;
    CS_Low();
    SPI_TxRx(MCP_READ);
    SPI_TxRx(reg);
    val = SPI_TxRx(0x00);
    CS_High();
    return val;
}

void MCP2515_WriteReg(uint8_t reg, uint8_t val) {
    CS_Low();
    SPI_TxRx(MCP_WRITE);
    SPI_TxRx(reg);
    SPI_TxRx(val);
    CS_High();
}

void MCP2515_BitModify(uint8_t reg, uint8_t mask, uint8_t val) {
    CS_Low();
    SPI_TxRx(MCP_BITMOD);
    SPI_TxRx(reg);
    SPI_TxRx(mask);
    SPI_TxRx(val);
    CS_High();
}

uint8_t MCP2515_Init(SPI_HandleTypeDef *hspi, GPIO_TypeDef *cs_port, uint16_t cs_pin) {
    p_hspi = hspi;
    p_cs_port = cs_port;
    p_cs_pin = cs_pin;

    CS_High(); // CS Idle State

    MCP2515_Reset();

    // Check communication by reading CANSTAT (should be in Configuration Mode after Reset)
    uint8_t mode = MCP2515_ReadReg(MCP_CANSTAT);
    if ((mode & MODE_MASK) != MODE_CONFIG) {
        return 0; // Communication fail
    }

    // Set Bitrate: 250 Kbps at 8MHz or 16MHz Crystal Oscillator (Sample Point = 75%)
    #if defined(MCP2515_CRYSTAL_16MHZ)
    MCP2515_WriteReg(MCP_CNF1, 0x03); // BRP = 3 for 16MHz -> 250 Kbps
    #else
    MCP2515_WriteReg(MCP_CNF1, 0x01); // BRP = 1 for 8MHz -> 250 Kbps
    #endif
    MCP2515_WriteReg(MCP_CNF2, 0x91); // BTLMODE=1, PHSEG1=3TQ, PRSEG=2TQ
    MCP2515_WriteReg(MCP_CNF3, 0x01); // PHSEG2=2TQ (Sample Point = 75.0%)

    // Enable RX interrupt
    MCP2515_WriteReg(MCP_CANINTE, 0x03); // Enable RX0IE, RX1IE

    // Set RX buffers to receive any message (disable masks/filters)
    MCP2515_WriteReg(MCP_RXB0CTRL, 0x60);
    MCP2515_WriteReg(MCP_RXB1CTRL, 0x60);

    // Set normal mode
    MCP2515_BitModify(MCP_CANCTRL, MODE_MASK, MODE_NORMAL);

    // Check mode
    mode = MCP2515_ReadReg(MCP_CANSTAT);
    if ((mode & MODE_MASK) != MODE_NORMAL) {
        return 0; // Failed to enter normal mode
    }

    return 1; // Success
}

uint8_t MCP2515_SendCANMessage(CAN_Message_t *msg) {
    // Check if TXB0 is busy
    uint8_t status = MCP2515_ReadReg(MCP_TXB0CTRL);
    uint32_t timeout = 10000;
    while ((status & 0x08) && timeout--) {
        status = MCP2515_ReadReg(MCP_TXB0CTRL);
    }
    if (status & 0x08) return 0; // TX Buffer Busy Timeout

    // Write Standard ID
    MCP2515_WriteReg(MCP_TXB0SIDH, (uint8_t)(msg->id >> 3));
    MCP2515_WriteReg(MCP_TXB0SIDL, (uint8_t)((msg->id & 0x07) << 5));

    // Write Data Length
    MCP2515_WriteReg(MCP_TXB0DLC, msg->dlc);

    // Write Data bytes
    for (uint8_t i = 0; i < msg->dlc; i++) {
        MCP2515_WriteReg(MCP_TXB0D0 + i, msg->data[i]);
    }

    // Request to send TXB0
    CS_Low();
    SPI_TxRx(MCP_RTS_TX0);
    CS_High();

    return 1;
}

uint8_t MCP2515_CheckReceive(void) {
    uint8_t intf = MCP2515_ReadReg(MCP_CANINTF);
    return (intf & 0x03) ? 1 : 0; // RX0IF or RX1IF set
}

uint8_t MCP2515_ReceiveCANMessage(CAN_Message_t *msg) {
    uint8_t intf = MCP2515_ReadReg(MCP_CANINTF);
    uint8_t buf = 0;

    if (intf & 0x01) {
        buf = 0; // RXB0
    } else if (intf & 0x02) {
        buf = 1; // RXB1
    } else {
        return 0; // No message received
    }

    uint8_t sidh_reg = (buf == 0) ? MCP_RXB0SIDH : MCP_RXB1SIDH;
    uint8_t sidl_reg = (buf == 0) ? MCP_RXB0SIDL : MCP_RXB1SIDL;
    uint8_t dlc_reg  = (buf == 0) ? MCP_RXB0DLC  : MCP_RXB1DLC;
    uint8_t d0_reg   = (buf == 0) ? MCP_RXB0D0   : MCP_RXB1D0;

    // Read ID (Standard 11-bit ID)
    uint16_t idh = MCP2515_ReadReg(sidh_reg);
    uint16_t idl = MCP2515_ReadReg(sidl_reg);
    msg->id = (idh << 3) | (idl >> 5);

    // Read DLC
    msg->dlc = MCP2515_ReadReg(dlc_reg) & 0x0F;

    // Read Data
    for (uint8_t i = 0; i < msg->dlc; i++) {
        msg->data[i] = MCP2515_ReadReg(d0_reg + i);
    }

    // Clear RX flag
    MCP2515_BitModify(MCP_CANINTF, (buf == 0) ? 0x01 : 0x02, 0x00);

    return 1;
}
