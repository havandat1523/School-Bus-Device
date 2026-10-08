#include "DFPLAYER.h"
#include "cmsis_os2.h"

static DFPLAYER_Name g_dfplayer;

static const uint8_t SendFrame[10] = {0x7E, 0xFF, 0x06, 0x00, 0x00,
                                      0x00, 0x00, 0x00, 0x00, 0xEF};

static void DFPLAYER_SendUART(DFPLAYER_Name *MP3) {
  HAL_UART_Transmit(MP3->DFP_UART, (uint8_t *)&MP3->SendBuff,
                    sizeof(MP3->SendBuff), 500);
}

static void DFPLAYER_Delay(uint16_t Time) {
  if (osKernelGetState() == osKernelRunning) {
    osDelay(Time);
  } else {
    HAL_Delay(Time);
  }
}

static uint16_t DFPLAYER_GetChecksum(uint8_t *thebuf) {
  uint16_t sum = 0;
  for (int i = 1; i < 7; i++) {
    sum += thebuf[i];
  }
  return -sum;
}

static void DFPLAYER_FillBigend(uint8_t *thebuf, uint16_t data) {
  *thebuf = (uint8_t)(data >> 8);
  *(thebuf + 1) = (uint8_t)data;
}

static void DFPLAYER_FillChecksum(DFPLAYER_Name *MP3) {
  uint16_t checksum = DFPLAYER_GetChecksum(MP3->SendBuff);
  DFPLAYER_FillBigend(MP3->SendBuff + 7, checksum);
}

static void DFPLAYER_SendCmd(DFPLAYER_Name *MP3, uint8_t cmd, uint16_t high_arg,
                             uint16_t low_arg) {
  DFPLAYER_Delay(50);
  MP3->SendBuff[3] = cmd;
  MP3->SendBuff[5] = high_arg;
  MP3->SendBuff[6] = low_arg;
  DFPLAYER_FillChecksum(MP3);
  DFPLAYER_SendUART(MP3);
}

void DFPLAYER_Init(DFPLAYER_Name *MP3, UART_HandleTypeDef *UART) {
  for (int i = 0; i < 10; i++) {
    MP3->SendBuff[i] = SendFrame[i];
  }
  MP3->DFP_UART = UART;
}

void DFPLAYER_PlayTrack(DFPLAYER_Name *MP3, uint16_t num) {
  uint8_t num1 = num >> 8;
  uint8_t num2 = num;
  DFPLAYER_SendCmd(MP3, DFP_PLAYTRACK, num1, num2);
}

void DFPLAYER_Next(DFPLAYER_Name *MP3) {
  DFPLAYER_SendCmd(MP3, DFP_NEXT, 0, 0);
}

void DFPLAYER_Prev(DFPLAYER_Name *MP3) {
  DFPLAYER_SendCmd(MP3, DFP_PREV, 0, 0);
}

void DFPLAYER_SetVolume(DFPLAYER_Name *MP3, uint16_t volume) {
  uint8_t volume1 = volume >> 8;
  uint8_t volume2 = volume;
  DFPLAYER_SendCmd(MP3, DFP_SETVOLUME, volume1, volume2);
}

void DFPLAYER_Play(DFPLAYER_Name *MP3) {
  DFPLAYER_SendCmd(MP3, DFP_PLAY, 0, 0);
}

void DFPLAYER_Pause(DFPLAYER_Name *MP3) {
  DFPLAYER_SendCmd(MP3, DFP_PAUSE, 0, 0);
}

void DFPLAYER_Stop(DFPLAYER_Name *MP3) {
  DFPLAYER_SendCmd(MP3, DFP_STOP, 0, 0);
}

void DFPLAYER_RandomPlay(DFPLAYER_Name *MP3) {
  DFPLAYER_SendCmd(MP3, DFP_RANDOM, 0, 0);
}

void DFPLAYER_PlayFileInFolder(DFPLAYER_Name *MP3, uint8_t folder, uint32_t num) {
  DFPLAYER_SendCmd(MP3, DFP_PLAYFILEINFOLDER, folder, num);
}

void DF_Init(UART_HandleTypeDef *huart) {
  DFPLAYER_Init(&g_dfplayer, huart);
}

void DF_Stop(void) {
  DFPLAYER_Stop(&g_dfplayer);
}

void DF_SetVolume(uint8_t volume) {
  DFPLAYER_SetVolume(&g_dfplayer, volume);
}

void DF_PlayFolder(uint8_t folder, uint8_t track) {
  DFPLAYER_PlayFileInFolder(&g_dfplayer, folder, track);
}
