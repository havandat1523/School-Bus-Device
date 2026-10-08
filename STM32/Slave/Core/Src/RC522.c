#include "RC522.h"
#include "spi.h"

// RC522 uses SPI2 on Slave node
extern SPI_HandleTypeDef hspi2;

// --- HÀM GIAO TIẾP SPI LÕI ---
uint8_t TM_SPI_Send(uint8_t data) {
  uint8_t rx_data = 0;
  HAL_SPI_TransmitReceive(&hspi2, &data, &rx_data, 1, 100);
  return rx_data;
}

// --- QUẢN LÝ CHÂN CHIP SELECT (Software NSS) ---
void MFRC522_CS_LOW(void) {
  HAL_GPIO_WritePin(RFID_CS_GPIO_Port, RFID_CS_Pin, GPIO_PIN_RESET);
}

void MFRC522_CS_HIGH(void) {
  HAL_GPIO_WritePin(RFID_CS_GPIO_Port, RFID_CS_Pin, GPIO_PIN_SET);
}

// ==========================================================
// CÁC HÀM XỬ LÝ LOGIC CỦA RC522
// ==========================================================

void TM_MFRC522_Init(void) {
  MFRC522_CS_HIGH(); // CS idle state

  // Hardware reset pulse via RST pin
  HAL_GPIO_WritePin(RFID_RST_GPIO_Port, RFID_RST_Pin, GPIO_PIN_RESET);
  HAL_Delay(2);
  HAL_GPIO_WritePin(RFID_RST_GPIO_Port, RFID_RST_Pin, GPIO_PIN_SET);
  HAL_Delay(5);

  TM_MFRC522_Reset();

  TM_MFRC522_WriteRegister(MFRC522_REG_T_MODE, 0x8D);
  TM_MFRC522_WriteRegister(MFRC522_REG_T_PRESCALER, 0x3E);
  TM_MFRC522_WriteRegister(MFRC522_REG_T_RELOAD_L, 30);
  TM_MFRC522_WriteRegister(MFRC522_REG_T_RELOAD_H, 0);

  /* 48dB gain */
  TM_MFRC522_WriteRegister(MFRC522_REG_RF_CFG, 0x70);

  TM_MFRC522_WriteRegister(MFRC522_REG_TX_AUTO, 0x40);
  TM_MFRC522_WriteRegister(MFRC522_REG_MODE, 0x3D);

  TM_MFRC522_AntennaOn(); // Open antenna
}

TM_MFRC522_Status_t TM_MFRC522_Check(uint8_t *id) {
  TM_MFRC522_Status_t status;
  // Find cards, return card type
  status = TM_MFRC522_Request(PICC_REQIDL, id);
  if (status == MI_OK) {
    // Card detected: Anti-collision, return card serial number 4 bytes
    status = TM_MFRC522_Anticoll(id);
  }
  TM_MFRC522_Halt(); // Command card into hibernation

  return status;
}

TM_MFRC522_Status_t TM_MFRC522_Compare(uint8_t *CardID, uint8_t *CompareID) {
  uint8_t i;
  for (i = 0; i < 5; i++) {
    if (CardID[i] != CompareID[i]) {
      return MI_ERR;
    }
  }
  return MI_OK;
}

void TM_MFRC522_WriteRegister(uint8_t addr, uint8_t val) {
  MFRC522_CS_LOW();
  TM_SPI_Send((addr << 1) & 0x7E);
  TM_SPI_Send(val);
  MFRC522_CS_HIGH();
}

uint8_t TM_MFRC522_ReadRegister(uint8_t addr) {
  uint8_t val;
  MFRC522_CS_LOW();
  TM_SPI_Send(((addr << 1) & 0x7E) | 0x80);
  val = TM_SPI_Send(MFRC522_DUMMY);
  MFRC522_CS_HIGH();
  return val;
}

void TM_MFRC522_SetBitMask(uint8_t reg, uint8_t mask) {
  TM_MFRC522_WriteRegister(reg, TM_MFRC522_ReadRegister(reg) | mask);
}

void TM_MFRC522_ClearBitMask(uint8_t reg, uint8_t mask) {
  TM_MFRC522_WriteRegister(reg, TM_MFRC522_ReadRegister(reg) & (~mask));
}

void TM_MFRC522_AntennaOn(void) {
  uint8_t temp;
  temp = TM_MFRC522_ReadRegister(MFRC522_REG_TX_CONTROL);
  if (!(temp & 0x03)) {
    TM_MFRC522_SetBitMask(MFRC522_REG_TX_CONTROL, 0x03);
  }
}

void TM_MFRC522_AntennaOff(void) {
  TM_MFRC522_ClearBitMask(MFRC522_REG_TX_CONTROL, 0x03);
}

void TM_MFRC522_Reset(void) {
  TM_MFRC522_WriteRegister(MFRC522_REG_COMMAND, PCD_RESETPHASE);
}

TM_MFRC522_Status_t TM_MFRC522_Request(uint8_t reqMode, uint8_t *TagType) {
  TM_MFRC522_Status_t status;
  uint16_t backBits;

  TM_MFRC522_WriteRegister(MFRC522_REG_BIT_FRAMING, 0x07);

  TagType[0] = reqMode;
  status = TM_MFRC522_ToCard(PCD_TRANSCEIVE, TagType, 1, TagType, &backBits);

  if ((status != MI_OK) || (backBits != 0x10)) {
    status = MI_ERR;
  }

  return status;
}

TM_MFRC522_Status_t TM_MFRC522_ToCard(uint8_t command, uint8_t *sendData,
                                      uint8_t sendLen, uint8_t *backData,
                                      uint16_t *backLen) {
  TM_MFRC522_Status_t status = MI_ERR;
  uint8_t irqEn = 0x00;
  uint8_t waitIRq = 0x00;
  uint8_t lastBits;
  uint8_t n;
  uint16_t i;

  switch (command) {
  case PCD_AUTHENT:
    irqEn = 0x12;
    waitIRq = 0x10;
    break;
  case PCD_TRANSCEIVE:
    irqEn = 0x77;
    waitIRq = 0x30;
    break;
  default:
    break;
  }

  TM_MFRC522_WriteRegister(MFRC522_REG_COMM_IE_N, irqEn | 0x80);
  TM_MFRC522_ClearBitMask(MFRC522_REG_COMM_IRQ, 0x80);
  TM_MFRC522_SetBitMask(MFRC522_REG_FIFO_LEVEL, 0x80);

  TM_MFRC522_WriteRegister(MFRC522_REG_COMMAND, PCD_IDLE);

  for (i = 0; i < sendLen; i++) {
    TM_MFRC522_WriteRegister(MFRC522_REG_FIFO_DATA, sendData[i]);
  }

  TM_MFRC522_WriteRegister(MFRC522_REG_COMMAND, command);
  if (command == PCD_TRANSCEIVE) {
    TM_MFRC522_SetBitMask(MFRC522_REG_BIT_FRAMING, 0x80);
  }

  i = 2000;
  do {
    n = TM_MFRC522_ReadRegister(MFRC522_REG_COMM_IRQ);
    i--;
  } while ((i != 0) && !(n & 0x01) && !(n & waitIRq));

  TM_MFRC522_ClearBitMask(MFRC522_REG_BIT_FRAMING, 0x80);

  if (i != 0) {
    if (!(TM_MFRC522_ReadRegister(MFRC522_REG_ERROR) & 0x1B)) {
      status = MI_OK;
      if (n & irqEn & 0x01) {
        status = MI_NOTAGERR;
      }

      if (command == PCD_TRANSCEIVE) {
        n = TM_MFRC522_ReadRegister(MFRC522_REG_FIFO_LEVEL);
        lastBits = TM_MFRC522_ReadRegister(MFRC522_REG_CONTROL) & 0x07;
        if (lastBits) {
          *backLen = (n - 1) * 8 + lastBits;
        } else {
          *backLen = n * 8;
        }

        if (n == 0) n = 1;
        if (n > MFRC522_MAX_LEN) n = MFRC522_MAX_LEN;

        for (i = 0; i < n; i++) {
          backData[i] = TM_MFRC522_ReadRegister(MFRC522_REG_FIFO_DATA);
        }
      }
    } else {
      status = MI_ERR;
    }
  }

  return status;
}

TM_MFRC522_Status_t TM_MFRC522_Anticoll(uint8_t *serNum) {
  TM_MFRC522_Status_t status;
  uint8_t i;
  uint8_t serNumCheck = 0;
  uint16_t unLen;

  TM_MFRC522_WriteRegister(MFRC522_REG_BIT_FRAMING, 0x00);

  serNum[0] = PICC_ANTICOLL;
  serNum[1] = 0x20;
  status = TM_MFRC522_ToCard(PCD_TRANSCEIVE, serNum, 2, serNum, &unLen);

  if (status == MI_OK) {
    for (i = 0; i < 4; i++) {
      serNumCheck ^= serNum[i];
    }
    if (serNumCheck != serNum[i]) {
      status = MI_ERR;
    }
  }
  return status;
}

void TM_MFRC522_CalculateCRC(uint8_t *pIndata, uint8_t len, uint8_t *pOutData) {
  uint8_t i, n;

  TM_MFRC522_ClearBitMask(MFRC522_REG_DIV_IRQ, 0x04);
  TM_MFRC522_SetBitMask(MFRC522_REG_FIFO_LEVEL, 0x80);

  for (i = 0; i < len; i++) {
    TM_MFRC522_WriteRegister(MFRC522_REG_FIFO_DATA, *(pIndata + i));
  }
  TM_MFRC522_WriteRegister(MFRC522_REG_COMMAND, PCD_CALCCRC);

  i = 0xFF;
  do {
    n = TM_MFRC522_ReadRegister(MFRC522_REG_DIV_IRQ);
    i--;
  } while ((i != 0) && !(n & 0x04));

  pOutData[0] = TM_MFRC522_ReadRegister(MFRC522_REG_CRC_RESULT_L);
  pOutData[1] = TM_MFRC522_ReadRegister(MFRC522_REG_CRC_RESULT_M);
}

void TM_MFRC522_Halt(void) {
  uint16_t unLen;
  uint8_t buff[4];

  buff[0] = PICC_HALT;
  buff[1] = 0;
  TM_MFRC522_CalculateCRC(buff, 2, &buff[2]);

  TM_MFRC522_ToCard(PCD_TRANSCEIVE, buff, 4, buff, &unLen);
}
