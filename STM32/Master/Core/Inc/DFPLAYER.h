#ifndef INC_DFPLAYER_H_
#define INC_DFPLAYER_H_

#include "stm32f4xx_hal.h"

typedef struct {
  UART_HandleTypeDef *DFP_UART;
  uint8_t SendBuff[10];
  uint16_t Checksum;
} DFPLAYER_Name;

#define DFP_PLAYTRACK        0x12
#define DFP_NEXT             0x01
#define DFP_PREV             0x02
#define DFP_SETVOLUME        0x06
#define DFP_PLAY             0x0D
#define DFP_PAUSE            0x0E
#define DFP_STOP             0x16
#define DFP_RANDOM           0x18
#define DFP_PLAYFILEINFOLDER 0x0F

void DFPLAYER_Init(DFPLAYER_Name *MP3, UART_HandleTypeDef *UART);
void DFPLAYER_PlayTrack(DFPLAYER_Name *MP3, uint16_t num);
void DFPLAYER_Next(DFPLAYER_Name *MP3);
void DFPLAYER_Prev(DFPLAYER_Name *MP3);
void DFPLAYER_SetVolume(DFPLAYER_Name *MP3, uint16_t volume);
void DFPLAYER_Play(DFPLAYER_Name *MP3);
void DFPLAYER_Pause(DFPLAYER_Name *MP3);
void DFPLAYER_Stop(DFPLAYER_Name *MP3);
void DFPLAYER_RandomPlay(DFPLAYER_Name *MP3);
void DFPLAYER_PlayFileInFolder(DFPLAYER_Name *MP3, uint8_t folder, uint32_t num);

/* Convenient wrappers */
void DF_Init(UART_HandleTypeDef *huart);
void DF_Stop(void);
void DF_SetVolume(uint8_t volume);
void DF_PlayFolder(uint8_t folder, uint8_t track);

#endif /* INC_DFPLAYER_H_ */
