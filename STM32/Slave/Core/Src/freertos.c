/* USER CODE BEGIN Header */
/**
  ******************************************************************************
  * File Name          : freertos.c
  * Description        : FreeRTOS implementation for STM32 Unified Slave Node
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
#include "MCP2515.h"
#include "RC522.h"
#include "hc165.h"
#include "dht11.h"
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
/* USER CODE END Variables */
/* Definitions for SOS_Alert */
osThreadId_t SOS_AlertHandle;
const osThreadAttr_t SOS_Alert_attributes = {
  .name = "SOS_Alert",
  .stack_size = 256 * 4,
  .priority = (osPriority_t) osPriorityRealtime,
};
/* Definitions for Seat_Read */
osThreadId_t Seat_ReadHandle;
const osThreadAttr_t Seat_Read_attributes = {
  .name = "Seat_Read",
  .stack_size = 384 * 4,
  .priority = (osPriority_t) osPriorityAboveNormal,
};
/* Definitions for RFID_Read */
osThreadId_t RFID_ReadHandle;
const osThreadAttr_t RFID_Read_attributes = {
  .name = "RFID_Read",
  .stack_size = 512 * 4,
  .priority = (osPriority_t) osPriorityNormal,
};
/* Definitions for DHT11_Read */
osThreadId_t DHT11_ReadHandle;
const osThreadAttr_t DHT11_Read_attributes = {
  .name = "DHT11_Read",
  .stack_size = 256 * 4,
  .priority = (osPriority_t) osPriorityBelowNormal,
};
/* Definitions for Heartbeat_Send */
osThreadId_t Heartbeat_SendHandle;
const osThreadAttr_t Heartbeat_Send_attributes = {
  .name = "Heartbeat_Send",
  .stack_size = 256 * 4,
  .priority = (osPriority_t) osPriorityLow,
};
/* Definitions for canSpiMutex */
osMutexId_t canSpiMutexHandle;
const osMutexAttr_t canSpiMutex_attributes = {
  .name = "canSpiMutex"
};
/* Definitions for sosSem */
osSemaphoreId_t sosSemHandle;
const osSemaphoreAttr_t sosSem_attributes = {
  .name = "sosSem"
};

/* Private function prototypes -----------------------------------------------*/
/* USER CODE BEGIN FunctionPrototypes */
// Thread-safe CAN transmit protected by canSpiMutex
static uint8_t Slave_SendCAN_Safe(CAN_Message_t *msg, uint32_t timeout_ms) {
  uint8_t res = 0;
  if (osMutexAcquire(canSpiMutexHandle, timeout_ms) == osOK) {
    res = MCP2515_SendCANMessage(msg);
    osMutexRelease(canSpiMutexHandle);
  }
  return res;
}

// EXTI callback: triggers immediately when SOS button (PA0) is pressed
void HAL_GPIO_EXTI_Callback(uint16_t GPIO_Pin) {
  if (GPIO_Pin == SOS_KEY_Pin) {
    osSemaphoreRelease(sosSemHandle);
  }
}
/* USER CODE END FunctionPrototypes */

void StartSosAlertTask(void *argument);
void StartSeatReadTask(void *argument);
void StartRfidTask(void *argument);
void StartDht11Task(void *argument);
void StartHeartbeatTask(void *argument);

void MX_FREERTOS_Init(void); /* (MISRA C 2004 rule 8.1) */

/**
  * @brief  FreeRTOS initialization
  * @param  None
  * @retval None
  */
void MX_FREERTOS_Init(void) {
  /* USER CODE BEGIN Init */

  /* USER CODE END Init */
  /* Create the mutex(es) */
  /* creation of canSpiMutex */
  canSpiMutexHandle = osMutexNew(&canSpiMutex_attributes);

  /* USER CODE BEGIN RTOS_MUTEX */
  /* add mutexes, ... */
  /* USER CODE END RTOS_MUTEX */

  /* Create the semaphores(s) */
  /* creation of sosSem */
  sosSemHandle = osSemaphoreNew(1, 0, &sosSem_attributes); // Initial 0 tokens

  /* USER CODE BEGIN RTOS_SEMAPHORES */
  /* add semaphores, ... */
  /* USER CODE END RTOS_SEMAPHORES */

  /* USER CODE BEGIN RTOS_TIMERS */
  /* start timers, add new ones, ... */
  /* USER CODE END RTOS_TIMERS */

  /* USER CODE BEGIN RTOS_QUEUES */
  /* add queues, ... */
  /* USER CODE END RTOS_QUEUES */

  /* Create the thread(s) */
  /* creation of SOS_Alert */
  SOS_AlertHandle = osThreadNew(StartSosAlertTask, NULL, &SOS_Alert_attributes);

  /* creation of Seat_Read */
  Seat_ReadHandle = osThreadNew(StartSeatReadTask, NULL, &Seat_Read_attributes);

  /* creation of RFID_Read */
  RFID_ReadHandle = osThreadNew(StartRfidTask, NULL, &RFID_Read_attributes);

  /* creation of DHT11_Read */
  DHT11_ReadHandle = osThreadNew(StartDht11Task, NULL, &DHT11_Read_attributes);

  /* creation of Heartbeat_Send */
  Heartbeat_SendHandle = osThreadNew(StartHeartbeatTask, NULL, &Heartbeat_Send_attributes);

  /* USER CODE BEGIN RTOS_THREADS */
  /* add threads, ... */
  /* USER CODE END RTOS_THREADS */

  /* USER CODE BEGIN RTOS_EVENTS */
  /* add events, ... */
  /* USER CODE END RTOS_EVENTS */

}

/* USER CODE BEGIN Header_StartSosAlertTask */
/**
  * @brief  Function implementing the SOS_Alert thread.
  *         Wakes immediately on EXTI interrupt, sends CAN ID 0x100.
  * @param  argument: Not used
  * @retval None
  */
/* USER CODE END Header_StartSosAlertTask */
void StartSosAlertTask(void *argument)
{
  /* USER CODE BEGIN StartSosAlertTask */
  uint8_t sos_active = 0;
  CAN_Message_t sos_msg;
  sos_msg.id = 0x100;
  sos_msg.dlc = 1;

  for(;;)
  {
    if (!sos_active)
    {
      // 1. Trạng thái chờ: Đợi sự kiện nhấn nút từ ISR
      if (osSemaphoreAcquire(sosSemHandle, osWaitForever) == osOK)
      {
        osDelay(50); // Software debounce chống dội phím
        if (HAL_GPIO_ReadPin(SOS_KEY_GPIO_Port, SOS_KEY_Pin) == GPIO_PIN_RESET)
        {
          uint8_t triggered = 0;
          uint32_t press_start = xTaskGetTickCount();

          // Kiểm tra Điều kiện 1: Nhấn GIỮ LÂU liên tục >= 3 giây (3000ms)
          while (HAL_GPIO_ReadPin(SOS_KEY_GPIO_Port, SOS_KEY_Pin) == GPIO_PIN_RESET)
          {
            osDelay(50);
            if ((xTaskGetTickCount() - press_start) >= pdMS_TO_TICKS(3000))
            {
              triggered = 1; // Đã giữ liên tục đủ 3 giây -> KÍCH HOẠT!
              break;
            }
          }

          // Nếu nhả nút ra trước 3 giây: Kiểm tra Điều kiện 2 (Trẻ hoảng loạn spam liên tục >= 3 lần trong 2.5s)
          if (!triggered)
          {
            uint8_t click_count = 1; // Đã ghi nhận 1 lần nhấn đầu tiên
            uint32_t window_start = xTaskGetTickCount();

            while ((xTaskGetTickCount() - window_start) < pdMS_TO_TICKS(2500))
            {
              uint32_t elapsed = xTaskGetTickCount() - window_start;
              uint32_t remaining = (pdMS_TO_TICKS(2500) > elapsed) ? (pdMS_TO_TICKS(2500) - elapsed) : 1;

              if (osSemaphoreAcquire(sosSemHandle, remaining) == osOK)
              {
                osDelay(40); // Debounce
                if (HAL_GPIO_ReadPin(SOS_KEY_GPIO_Port, SOS_KEY_Pin) == GPIO_PIN_RESET)
                {
                  uint32_t sub_press = xTaskGetTickCount();
                  // Nếu trong lúc spam lại chuyển sang giữ lâu >= 3s
                  while (HAL_GPIO_ReadPin(SOS_KEY_GPIO_Port, SOS_KEY_Pin) == GPIO_PIN_RESET)
                  {
                    osDelay(50);
                    if ((xTaskGetTickCount() - sub_press) >= pdMS_TO_TICKS(3000))
                    {
                      triggered = 1;
                      break;
                    }
                  }

                  if (triggered) break;

                  click_count++;
                  if (click_count >= 3)
                  {
                    triggered = 1; // Nhấn dồn dập >= 3 lần -> KÍCH HOẠT KHẨN CẤP!
                    break;
                  }
                }
              }
            }
          }

          // Xử lý khi thỏa mãn điều kiện kích hoạt SOS:
          if (triggered)
          {
            sos_active = 1;
            sos_msg.data[0] = 0x01; // SOS Active
            Slave_SendCAN_Safe(&sos_msg, 200);

            // Chờ nhả nút hoàn toàn
            while (HAL_GPIO_ReadPin(SOS_KEY_GPIO_Port, SOS_KEY_Pin) == GPIO_PIN_RESET)
            {
              osDelay(20);
            }
            while (osSemaphoreAcquire(sosSemHandle, 0) == osOK);
          }
          else
          {
            // Bấm nhầm thoáng qua (< 3s và < 3 lần) -> Bỏ qua, xóa token thừa
            while (osSemaphoreAcquire(sosSemHandle, 0) == osOK);
          }
        }
      }
    }
    else
    {
      // 2. Trạng thái SOS đang kích hoạt: Còi kêu liên tục dồn dập (150ms BẬT / 100ms TẮT)
      //    Để HỦY BÁO ĐỘNG (tắt còi): Người lớn nhấn giữ nút >= 1.5 giây
      HAL_GPIO_WritePin(BUZZER_GPIO_Port, BUZZER_Pin, GPIO_PIN_SET);
      if (osSemaphoreAcquire(sosSemHandle, 150) == osOK)
      {
        osDelay(50);
        if (HAL_GPIO_ReadPin(SOS_KEY_GPIO_Port, SOS_KEY_Pin) == GPIO_PIN_RESET)
        {
          uint32_t cancel_start = xTaskGetTickCount();
          uint8_t cancel_ok = 0;
          while (HAL_GPIO_ReadPin(SOS_KEY_GPIO_Port, SOS_KEY_Pin) == GPIO_PIN_RESET)
          {
            osDelay(50);
            if ((xTaskGetTickCount() - cancel_start) >= pdMS_TO_TICKS(1500))
            {
              cancel_ok = 1;
              break;
            }
          }
          if (cancel_ok)
          {
            sos_active = 0;
            HAL_GPIO_WritePin(BUZZER_GPIO_Port, BUZZER_Pin, GPIO_PIN_RESET);
            CAN_Message_t cancel_msg;
            cancel_msg.id = 0x101; // F3: CAN 0x101 (Hủy SOS)
            cancel_msg.dlc = 1;
            cancel_msg.data[0] = 0x00;
            Slave_SendCAN_Safe(&cancel_msg, 200);

            while (HAL_GPIO_ReadPin(SOS_KEY_GPIO_Port, SOS_KEY_Pin) == GPIO_PIN_RESET)
            {
              osDelay(20);
            }
            while (osSemaphoreAcquire(sosSemHandle, 0) == osOK);
            continue;
          }
        }
      }

      HAL_GPIO_WritePin(BUZZER_GPIO_Port, BUZZER_Pin, GPIO_PIN_RESET);
      if (osSemaphoreAcquire(sosSemHandle, 100) == osOK)
      {
        osDelay(50);
        if (HAL_GPIO_ReadPin(SOS_KEY_GPIO_Port, SOS_KEY_Pin) == GPIO_PIN_RESET)
        {
          uint32_t cancel_start = xTaskGetTickCount();
          uint8_t cancel_ok = 0;
          while (HAL_GPIO_ReadPin(SOS_KEY_GPIO_Port, SOS_KEY_Pin) == GPIO_PIN_RESET)
          {
            osDelay(50);
            if ((xTaskGetTickCount() - cancel_start) >= pdMS_TO_TICKS(1000))
            {
              cancel_ok = 1;
              break;
            }
          }
          if (cancel_ok)
          {
            sos_active = 0;
            HAL_GPIO_WritePin(BUZZER_GPIO_Port, BUZZER_Pin, GPIO_PIN_RESET);
            CAN_Message_t cancel_msg;
            cancel_msg.id = 0x101; // F3: CAN 0x101 (Hủy SOS)
            cancel_msg.dlc = 1;
            cancel_msg.data[0] = 0x00;
            Slave_SendCAN_Safe(&cancel_msg, 200);

            while (HAL_GPIO_ReadPin(SOS_KEY_GPIO_Port, SOS_KEY_Pin) == GPIO_PIN_RESET)
            {
              osDelay(20);
            }
            while (osSemaphoreAcquire(sosSemHandle, 0) == osOK);
            continue;
          }
        }
      }
    }
  }
  /* USER CODE END StartSosAlertTask */
}

/* USER CODE BEGIN Header_StartSeatReadTask */
/**
* @brief Function implementing the Seat_Read thread.
*        Reads 16 seats via 2x 74HC165 every 100ms.
*        Implements 10-second debounce logic according to spec section 2.5.
*        Transmits CAN ID 0x120 on state change and every 2 seconds.
* @param argument: Not used
* @retval None
*/
/* USER CODE END Header_StartSeatReadTask */
void StartSeatReadTask(void *argument)
{
  /* USER CODE BEGIN StartSeatReadTask */
  HC165_Init();
  osDelay(500);

  CAN_Message_t seat_msg;
  seat_msg.id = 0x120;
  seat_msg.dlc = 2;

  uint16_t debounced_map = 0;
  TickType_t last_periodic_tx = 0;

  for(;;)
  {
    uint8_t state_changed = HC165_UpdateDebounce(&debounced_map);
    TickType_t now = xTaskGetTickCount();

    // Send CAN frame if state changed OR every 2 seconds periodically
    if (state_changed || (now - last_periodic_tx >= pdMS_TO_TICKS(2000)))
    {
      last_periodic_tx = now;
      seat_msg.data[0] = (uint8_t)((debounced_map >> 8) & 0xFF);
      seat_msg.data[1] = (uint8_t)(debounced_map & 0xFF);
      Slave_SendCAN_Safe(&seat_msg, 100);
    }

    osDelay(100);
  }
  /* USER CODE END StartSeatReadTask */
}

/* USER CODE BEGIN Header_StartRfidTask */
/**
* @brief Function implementing the RFID_Read thread.
*        Polls single RC522 reader on SPI2 (used for both Students and Attendants).
*        Transmits 4-byte card UID via CAN ID 0x200 upon successful swipe.
* @param argument: Not used
* @retval None
*/
/* USER CODE END Header_StartRfidTask */
void StartRfidTask(void *argument)
{
  /* USER CODE BEGIN StartRfidTask */
  TM_MFRC522_Init();
  osDelay(500);

  uint8_t card_id[5];
  CAN_Message_t rfid_msg;
  rfid_msg.id = 0x200;
  rfid_msg.dlc = 5;

  uint32_t last_scan_tick = 0;

  for(;;)
  {
    // Check if card is present at the reader
    if (TM_MFRC522_Check(card_id) == MI_OK)
    {
      // F2: Chống đọc lặp tại Slave — chỉ gửi CAN 0x200 nếu cách lần gửi trước > 2500ms
      uint32_t now = HAL_GetTick();
      if (now - last_scan_tick > 2500)
      {
        memcpy(rfid_msg.data, card_id, 5);
        if (Slave_SendCAN_Safe(&rfid_msg, 200) == HAL_OK)
        {
          last_scan_tick = now;
        }
      }

      // Cooldown
      osDelay(500);
    }

    osDelay(100);
  }
  /* USER CODE END StartRfidTask */
}

/* USER CODE BEGIN Header_StartDht11Task */
/**
* @brief Function implementing the DHT11_Read thread.
*        Reads temperature and humidity every 5 seconds.
*        Transmits CAN ID 0x210.
* @param argument: Not used
* @retval None
*/
/* USER CODE END Header_StartDht11Task */
void StartDht11Task(void *argument)
{
  /* USER CODE BEGIN StartDht11Task */
  DHT11_Init();
  osDelay(1000);

  DHT11_Data_t dht;
  CAN_Message_t dht_msg;
  dht_msg.id = 0x210;
  dht_msg.dlc = 4;

  for(;;)
  {
    if (!DHT11_Read(&dht))
    {
      // Fallback default readings (25.0 C, 50.0%)
      dht.temp_int = 25;
      dht.temp_dec = 0;
      dht.hum_int  = 50;
      dht.hum_dec  = 0;
    }

    dht_msg.data[0] = dht.temp_int;
    dht_msg.data[1] = dht.temp_dec;
    dht_msg.data[2] = dht.hum_int;
    dht_msg.data[3] = dht.hum_dec;

    Slave_SendCAN_Safe(&dht_msg, 200);

    osDelay(5000);
  }
  /* USER CODE END StartDht11Task */
}

/* USER CODE BEGIN Header_StartHeartbeatTask */
/**
* @brief Function implementing the Heartbeat_Send thread.
*        Transmits periodic heartbeat CAN ID 0x220 every 1.5 seconds
*        so Master can monitor connection liveness.
* @param argument: Not used
* @retval None
*/
/* USER CODE END Header_StartHeartbeatTask */
void StartHeartbeatTask(void *argument)
{
  /* USER CODE BEGIN StartHeartbeatTask */
  // Initialize MCP2515 CAN controller on SPI1
  MCP2515_Init(&hspi1, CAN_CS_GPIO_Port, CAN_CS_Pin);
  osDelay(1000);

  CAN_Message_t hb_msg;
  hb_msg.id = 0x220;
  hb_msg.dlc = 7;
  memcpy(hb_msg.data, "SLV_ALV", 7);

  for(;;)
  {
    Slave_SendCAN_Safe(&hb_msg, 200);
    osDelay(1500);
  }
  /* USER CODE END StartHeartbeatTask */
}

/* Private application code --------------------------------------------------*/
/* USER CODE BEGIN Application */

/* USER CODE END Application */
