/* USER CODE BEGIN Header */
/**
  ******************************************************************************
  * File Name          : freertos.c
  * Description        : FreeRTOS implementation for STM32 Master Node
  ******************************************************************************
  * @attention
  *
  * Copyright (c) 2026 STMicroelectronics.
  * All rights reserved.
  *
  ******************************************************************************
  */
/* USER CODE END Header */

/* Includes ------------------------------------------------------------------*/
#include "FreeRTOS.h"
#include "task.h"
#include "main.h"
#include "cmsis_os.h"

/* Private includes ----------------------------------------------------------*/
/* USER CODE BEGIN Includes */
#include "spi.h"
#include "usart.h"
#include "MCP2515.h"
#include "DFPLAYER.h"
#include "gps_parser.h"
#include "pi_protocol.h"
#include "audio_queue.h"
#include <stdio.h>
#include <string.h>
/* USER CODE END Includes */

/* Private typedef -----------------------------------------------------------*/
/* USER CODE BEGIN PTD */
/* USER CODE END PTD */

/* Private define ------------------------------------------------------------*/
/* USER CODE BEGIN PD */
/* USER CODE END PD */

/* Private macro -------------------------------------------------------------*/
/* USER CODE BEGIN PM */
/* USER CODE END PM */

/* Private variables ---------------------------------------------------------*/
/* USER CODE BEGIN Variables */
volatile uint8_t can_rx_flag = 0;
volatile uint32_t last_slave_heartbeat_tick = 0;
volatile uint8_t is_critical_alarm_active = 0;
volatile uint32_t alarm_last_tick = 0;
/* USER CODE END Variables */
/* Definitions for UART_Pi */
osThreadId_t UART_PiHandle;
const osThreadAttr_t UART_Pi_attributes = {
  .name = "UART_Pi",
  .stack_size = 512 * 4,
  .priority = (osPriority_t) osPriorityAboveNormal,
};
/* Definitions for CAN_Rx */
osThreadId_t CAN_RxHandle;
const osThreadAttr_t CAN_Rx_attributes = {
  .name = "CAN_Rx",
  .stack_size = 512 * 4,
  .priority = (osPriority_t) osPriorityHigh,
};
/* Definitions for GNSS_Parse */
osThreadId_t GNSS_ParseHandle;
const osThreadAttr_t GNSS_Parse_attributes = {
  .name = "GNSS_Parse",
  .stack_size = 512 * 4,
  .priority = (osPriority_t) osPriorityNormal,
};
/* Definitions for Audio_DFPlayer */
osThreadId_t Audio_DFPlayerHandle;
const osThreadAttr_t Audio_DFPlayer_attributes = {
  .name = "Audio_DFPlayer",
  .stack_size = 384 * 4,
  .priority = (osPriority_t) osPriorityNormal,
};
/* Definitions for Heartbeat_Mon */
osThreadId_t Heartbeat_MonHandle;
const osThreadAttr_t Heartbeat_Mon_attributes = {
  .name = "Heartbeat_Mon",
  .stack_size = 256 * 4,
  .priority = (osPriority_t) osPriorityLow,
};
/* Definitions for queueCanToPi */
osMessageQueueId_t queueCanToPiHandle;
const osMessageQueueAttr_t queueCanToPi_attributes = {
  .name = "queueCanToPi"
};
/* Definitions for queueAudio */
osMessageQueueId_t queueAudioHandle;
const osMessageQueueAttr_t queueAudio_attributes = {
  .name = "queueAudio"
};

/* Private function prototypes -----------------------------------------------*/
/* USER CODE BEGIN FunctionPrototypes */
void HAL_GPIO_EXTI_Callback(uint16_t GPIO_Pin) {
  if (GPIO_Pin == CAN_INT_Pin) {
    can_rx_flag = 1;
  }
}
/* USER CODE END FunctionPrototypes */

void StartUartPiTask(void *argument);
void StartCanRxTask(void *argument);
void StartGnssParseTask(void *argument);
void StartAudioTask(void *argument);
void StartHeartbeatMonTask(void *argument);

void MX_FREERTOS_Init(void); /* (MISRA C 2004 rule 8.1) */

/**
  * @brief  FreeRTOS initialization
  * @param  None
  * @retval None
  */
void MX_FREERTOS_Init(void) {
  /* USER CODE BEGIN Init */

  /* USER CODE END Init */

  /* USER CODE BEGIN RTOS_MUTEX */
  /* add mutexes, ... */
  /* USER CODE END RTOS_MUTEX */

  /* USER CODE BEGIN RTOS_SEMAPHORES */
  /* add semaphores, ... */
  /* USER CODE END RTOS_SEMAPHORES */

  /* USER CODE BEGIN RTOS_TIMERS */
  /* start timers, add new ones, ... */
  /* USER CODE END RTOS_TIMERS */

  /* Create the queue(s) */
  /* creation of queueCanToPi */
  queueCanToPiHandle = osMessageQueueNew (16, 16, &queueCanToPi_attributes);

  /* creation of queueAudio */
  queueAudioHandle = osMessageQueueNew (8, sizeof(uint16_t), &queueAudio_attributes);

  /* USER CODE BEGIN RTOS_QUEUES */
  /* add queues, ... */
  /* USER CODE END RTOS_QUEUES */

  /* Create the thread(s) */
  /* creation of UART_Pi */
  UART_PiHandle = osThreadNew(StartUartPiTask, NULL, &UART_Pi_attributes);

  /* creation of CAN_Rx */
  CAN_RxHandle = osThreadNew(StartCanRxTask, NULL, &CAN_Rx_attributes);

  /* creation of GNSS_Parse */
  GNSS_ParseHandle = osThreadNew(StartGnssParseTask, NULL, &GNSS_Parse_attributes);

  /* creation of Audio_DFPlayer */
  Audio_DFPlayerHandle = osThreadNew(StartAudioTask, NULL, &Audio_DFPlayer_attributes);

  /* creation of Heartbeat_Mon */
  Heartbeat_MonHandle = osThreadNew(StartHeartbeatMonTask, NULL, &Heartbeat_Mon_attributes);

  /* USER CODE BEGIN RTOS_THREADS */
  /* add threads, ... */
  /* USER CODE END RTOS_THREADS */

  /* USER CODE BEGIN RTOS_EVENTS */
  /* add events, ... */
  /* USER CODE END RTOS_EVENTS */

}

/* USER CODE BEGIN Header_StartUartPiTask */
/**
  * @brief  Function implementing the UART_Pi thread.
  *         Receives commands from Pi (USART1), controls DFPlayer,
  *         cancels repeating alarms, and sends responses.
  * @param  argument: Not used
  * @retval None
  */
/* USER CODE END Header_StartUartPiTask */
#define PI_DMA_RX_BUF_SIZE 128
static uint8_t pi_dma_rx_buf[PI_DMA_RX_BUF_SIZE];
static uint16_t pi_dma_rx_tail = 0;

void StartUartPiTask(void *argument)
{
  /* USER CODE BEGIN StartUartPiTask */
  PiProtocol_Init(&huart1);
  PiFrame_t rx_frame;

  // Start continuous circular DMA reception for USART1 from Pi / ESP32
  __HAL_UART_CLEAR_OREFLAG(&huart1);
  HAL_UART_Receive_DMA(&huart1, pi_dma_rx_buf, PI_DMA_RX_BUF_SIZE);

  for(;;)
  {
    // Re-arm DMA if it ever stopped due to error
    if (huart1.RxState == HAL_UART_STATE_READY)
    {
      __HAL_UART_CLEAR_OREFLAG(&huart1);
      HAL_UART_Receive_DMA(&huart1, pi_dma_rx_buf, PI_DMA_RX_BUF_SIZE);
    }

    // Current position where DMA is writing
    uint16_t dma_head = PI_DMA_RX_BUF_SIZE - (uint16_t)__HAL_DMA_GET_COUNTER(huart1.hdmarx);

    // Process all pending bytes from circular buffer
    while (pi_dma_rx_tail != dma_head)
    {
      uint8_t byte = pi_dma_rx_buf[pi_dma_rx_tail];
      pi_dma_rx_tail = (pi_dma_rx_tail + 1) % PI_DMA_RX_BUF_SIZE;

      if (PiProtocol_ProcessByte(byte, &rx_frame))
      {
        if (rx_frame.main_evt == 0xF4)
        {
          // F8: Hủy Critical Repeating Alarm 08/001
          is_critical_alarm_active = 0;
          AudioQueue_SetLoop0801(0);
          DF_Stop();
        }
        else if (rx_frame.main_evt == 0xF3)
        {
          // F8: SOS từ Pi (UI / GPIO kích hoạt, source=2 hoặc 3)
          // Nếu source != 1 (không phải từ Slave vì Master đã tự phát), push 07/001 vào queue
          uint8_t source = (rx_frame.len > 0) ? rx_frame.data[0] : 2;
          if (rx_frame.sub_evt == 0x01 && source != 1)
          {
            AudioQueue_Push(0x07, 0x01);
          }
          else if (rx_frame.sub_evt == 0x02)
          {
            // Hủy SOS
            DF_Stop();
          }
        }
        else if (rx_frame.main_evt <= 0x08)
        {
          // F8: Đưa vào hàng đợi ưu tiên audio_queue
          AudioQueue_Push(rx_frame.main_evt, rx_frame.sub_evt);

          if (rx_frame.main_evt == 0x08 && rx_frame.sub_evt == 0x01)
          {
            // Kích hoạt chu kỳ lặp lại cảnh báo nguy cấp 08/001
            is_critical_alarm_active = 1;
            AudioQueue_SetLoop0801(1);
            alarm_last_tick = xTaskGetTickCount();
          }
        }
      }
    }

    osDelay(5);
  }
  /* USER CODE END StartUartPiTask */
}

/* USER CODE BEGIN Header_StartCanRxTask */
/**
* @brief Function implementing the CAN_Rx thread.
*        Receives CAN frames from Slave, decodes by CAN ID,
*        and forwards data frames to Raspberry Pi via UART1.
* @param argument: Not used
* @retval None
*/
/* USER CODE END Header_StartCanRxTask */
void StartCanRxTask(void *argument)
{
  /* USER CODE BEGIN StartCanRxTask */
  CAN_Message_t rx_msg;

  // Initialize MCP2515 CAN controller
  uint8_t can_ok = MCP2515_Init(&hspi2, CAN_CS_GPIO_Port, CAN_CS_Pin);

  for(;;)
  {
    // If CAN controller is not connected/initialized, yield CPU and retry periodically
    if (!can_ok)
    {
      osDelay(2000);
      can_ok = MCP2515_Init(&hspi2, CAN_CS_GPIO_Port, CAN_CS_Pin);
      continue;
    }

    if (can_rx_flag || MCP2515_CheckReceive())
    {
      can_rx_flag = 0;
      uint8_t max_msgs = 8; // Prevent infinite loop if SPI noise occurs
      while (max_msgs-- && MCP2515_ReceiveCANMessage(&rx_msg))
      {
        if (rx_msg.id == 0x100)
        {
          // 1. SOS Active from Slave -> EVENT_MAIN = 0xF3, sub_evt = 0x01, source = 1 (slave)
          uint8_t src = 1;
          PiProtocol_SendFrame(0xF3, 0x01, 1, &src);

          // F3 & F8: Kích hoạt loa khoang lái DFPlayer phát track 07/001 ngay lập tức qua hàng đợi ưu tiên 3 (không chờ Pi)
          AudioQueue_Push(0x07, 0x01);
        }
        else if (rx_msg.id == 0x101)
        {
          // F3 & F8: Hủy SOS từ Slave -> dừng âm thanh DFPlayer, báo lên Pi UART 0xF3, sub_evt = 0x02
          DF_Stop();
          uint8_t src = 1;
          PiProtocol_SendFrame(0xF3, 0x02, 1, &src);
        }
        else if (rx_msg.id == 0x120)
        {
          // 2. 16 seats bitmap update (2 bytes) -> EVENT_MAIN = 0xF1
          uint16_t seat_map = (rx_msg.data[0] << 8) | rx_msg.data[1];
          char seat_str[100];
          int offset = 0;
          for (int i = 1; i <= 16; i++) {
            int occupied = (seat_map >> (i - 1)) & 0x01;
            offset += snprintf(seat_str + offset, sizeof(seat_str) - offset, "s%d:%d", i, occupied);
          }
          PiProtocol_SendFrame(0xF1, 0x00, (uint8_t)strlen(seat_str), (const uint8_t*)seat_str);
        }
        else if (rx_msg.id == 0x200)
        {
          // F2 & F8: Phát ngay 05/001 ("Đã quẹt thẻ") qua audio_queue (ưu tiên 2), không chờ Pi
          AudioQueue_Push(0x05, 0x01);

          // 3. RFID Card UID (hỗ trợ đầy đủ thẻ 4, 5, hoặc 7 byte UID) -> EVENT_MAIN = 0xF2
          char rfid_str[24];
          int offset = 0;
          uint8_t uid_len = (rx_msg.dlc >= 4 && rx_msg.dlc <= 8) ? rx_msg.dlc : 5;
          for (uint8_t i = 0; i < uid_len; i++)
          {
            offset += snprintf(rfid_str + offset, sizeof(rfid_str) - offset, "%02X", rx_msg.data[i]);
          }
          PiProtocol_SendFrame(0xF2, 0x00, (uint8_t)strlen(rfid_str), (const uint8_t*)rfid_str);
        }
        else if (rx_msg.id == 0x210)
        {
          // 4. DHT11 Temp & Humidity -> EVENT_MAIN = 0xF5
          char dht_str[32];
          snprintf(dht_str, sizeof(dht_str), "%d.%d,%d.%d",
                   rx_msg.data[0], rx_msg.data[1], rx_msg.data[2], rx_msg.data[3]);
          PiProtocol_SendFrame(0xF5, 0x00, (uint8_t)strlen(dht_str), (const uint8_t*)dht_str);
        }
        else if (rx_msg.id == 0x220)
        {
          // 5. Slave Heartbeat update
          last_slave_heartbeat_tick = xTaskGetTickCount();
        }
      }
    }
    osDelay(10);
  }
  /* USER CODE END StartCanRxTask */
}

/* USER CODE BEGIN Header_StartGnssParseTask */
/**
* @brief Function implementing the GNSS_Parse thread.
*        Parses NMEA sentences from ATGM336H (USART2) and
*        sends coordinates/speed to Pi via UART1 (EVENT_MAIN = 0xF0).
* @param argument: Not used
* @retval None
*/
/* USER CODE END Header_StartGnssParseTask */
void StartGnssParseTask(void *argument)
{
  /* USER CODE BEGIN StartGnssParseTask */
  char gps_line[GPS_BUF_SIZE];
  uint8_t gps_idx = 0;
  uint8_t ch = 0;
  GPS_Data_t gps_data;
  uint32_t last_telemetry_tick = 0;

  // Send initial default fallback location
  GPS_GetDefaultPayload(gps_data.payload, sizeof(gps_data.payload));
  PiProtocol_SendFrame(0xF0, 0x00, (uint8_t)strlen(gps_data.payload), (const uint8_t*)gps_data.payload);

  for(;;)
  {
    // Clear any overrun or framing error that may have occurred
    __HAL_UART_CLEAR_OREFLAG(&huart2);
    __HAL_UART_CLEAR_NEFLAG(&huart2);
    __HAL_UART_CLEAR_FEFLAG(&huart2);

    uint8_t byte_received = 0;
    while (HAL_UART_Receive(&huart2, &ch, 1, 5) == HAL_OK)
    {
      byte_received = 1;
      if (ch == '$')
      {
        gps_idx = 0;
        gps_line[gps_idx++] = ch;
      }
      else if (gps_idx > 0)
      {
        if (ch == '\r' || ch == '\n')
        {
          gps_line[gps_idx] = '\0';
          if (gps_idx > 10 && gps_line[0] == '$')
          {
            if (GPS_ParseNMEA(gps_line, &gps_data))
            {
              // Valid fix obtained: send immediately or throttle to 1s
              if (xTaskGetTickCount() - last_telemetry_tick >= pdMS_TO_TICKS(1000))
              {
                last_telemetry_tick = xTaskGetTickCount();
                PiProtocol_SendFrame(0xF0, 0x00, (uint8_t)strlen(gps_data.payload), (const uint8_t*)gps_data.payload);
              }
            }
          }
          gps_idx = 0;
          break; // Finished receiving a sentence, allow loop to continue
        }
        else if (gps_idx < GPS_BUF_SIZE - 1)
        {
          gps_line[gps_idx++] = ch;
        }
        else
        {
          gps_idx = 0;
        }
      }
    }

    if (!byte_received)
    {
      // Fallback periodic transmission every 5s if GPS module hasn't responded
      if (xTaskGetTickCount() - last_telemetry_tick >= pdMS_TO_TICKS(5000))
      {
        last_telemetry_tick = xTaskGetTickCount();
        GPS_GetDefaultPayload(gps_data.payload, sizeof(gps_data.payload));
        PiProtocol_SendFrame(0xF0, 0x00, (uint8_t)strlen(gps_data.payload), (const uint8_t*)gps_data.payload);
      }
      osDelay(5);
    }
  }
  /* USER CODE END StartGnssParseTask */
}

/* USER CODE BEGIN Header_StartAudioTask */
/**
* @brief Function implementing the Audio_DFPlayer thread.
*        Plays sound tracks on DFPlayer Mini according to
*        commands received from queueAudio, and handles alarm loop.
* @param argument: Not used
* @retval None
*/
/* USER CODE END Header_StartAudioTask */
void StartAudioTask(void *argument)
{
  /* USER CODE BEGIN StartAudioTask */
  AudioQueue_Init();
  DF_Init(&huart6);
  osDelay(1800); // Cho DFPlayer va the SD khoi dong hoan tat

  DF_SetVolume(26); // Set volume am luong lon ro rang
  osDelay(100);

  // Startup test: play welcome track 01/001 qua audio_queue
  AudioQueue_Push(0x01, 0x01);

  AudioItem_t current_item = {0};
  uint8_t is_playing = 0;
  uint32_t play_start_tick = 0;
  uint32_t play_duration_ticks = 0;

  for(;;)
  {
    uint32_t now = xTaskGetTickCount();

    // 1. Kiem tra track dang phat da het thoi gian chua (Master tu quan ly thoi gian <= 4s)
    if (is_playing)
    {
      if ((now - play_start_tick) >= play_duration_ticks)
      {
        // Track da phat xong
        is_playing = 0;
        current_item.priority = 0;
      }
    }

    // 2. Kiem tra preemption: neu trong queue co track co priority cao hon track dang phat
    uint8_t next_prio = AudioQueue_PeekPriority();
    if (is_playing && (next_prio > current_item.priority))
    {
      // Ngat track dang phat de nhuong quyen uu tien cao hon
      DF_Stop();
      osDelay(30);
      is_playing = 0;
      current_item.priority = 0;
    }

    // 3. Neu dang ranh, lay track tiep theo tu queue de phat
    if (!is_playing)
    {
      AudioItem_t next_item;
      if (AudioQueue_Pop(&next_item))
      {
        current_item = next_item;
        is_playing = 1;
        play_start_tick = xTaskGetTickCount();
        play_duration_ticks = pdMS_TO_TICKS(next_item.duration_ms);

        DF_PlayFolder(next_item.main_evt, next_item.sub_evt);
      }
      else if (AudioQueue_IsLoop0801())
      {
        // 4. Khi queue trong va co lap 08/001 dang kich hoat (Critical Alarm lap moi ~4.5s)
        if ((now - alarm_last_tick) >= pdMS_TO_TICKS(4500))
        {
          alarm_last_tick = xTaskGetTickCount();
          current_item.main_evt = 0x08;
          current_item.sub_evt = 0x01;
          current_item.priority = AUDIO_PRIO_CRITICAL;
          current_item.duration_ms = 4000;
          is_playing = 1;
          play_start_tick = xTaskGetTickCount();
          play_duration_ticks = pdMS_TO_TICKS(4000);

          DF_PlayFolder(0x08, 0x01);
        }
      }
    }

    osDelay(20);
  }
  /* USER CODE END StartAudioTask */
}

/* USER CODE BEGIN Header_StartHeartbeatMonTask */
/**
* @brief Function implementing the Heartbeat_Mon thread.
*        Monitors CAN heartbeat from Slave node (CAN ID 0x220).
*        Alerts Pi if heartbeat is lost for more than 3.5s.
* @param argument: Not used
* @retval None
*/
/* USER CODE END Header_StartHeartbeatMonTask */
void StartHeartbeatMonTask(void *argument)
{
  /* USER CODE BEGIN StartHeartbeatMonTask */
  // Give Slave 5s boot margin before monitoring
  osDelay(5000);

  for(;;)
  {
    osDelay(1000);
    if (last_slave_heartbeat_tick != 0)
    {
      uint32_t current_tick = xTaskGetTickCount();
      if ((current_tick - last_slave_heartbeat_tick) > pdMS_TO_TICKS(3500))
      {
        // Slave disconnected / bus lost
        char err_msg[] = "SLAVE_OFFLINE";
        PiProtocol_SendFrame(0x08, 0x02, (uint8_t)strlen(err_msg), (const uint8_t*)err_msg);
      }
    }
  }
  /* USER CODE END StartHeartbeatMonTask */
}

/* Private application code --------------------------------------------------*/
/* USER CODE BEGIN Application */

/* USER CODE END Application */
