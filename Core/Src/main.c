/* USER CODE BEGIN Header */
/**
  ******************************************************************************
  * @file           : main.c
  * @brief          : Main program body
  ******************************************************************************
  * @attention
  *
  * Copyright (c) 2026 STMicroelectronics.
  * All rights reserved.
  *
  * This software is licensed under terms that can be found in the LICENSE file
  * in the root directory of this software component.
  * If no LICENSE file comes with this software, it is provided AS-IS.
  *
  ******************************************************************************
  */
/* USER CODE END Header */
/* Includes ------------------------------------------------------------------*/
#include "main.h"
#include "spi.h"
#include "tim.h"
#include "gpio.h"

/* Private includes ----------------------------------------------------------*/
/* USER CODE BEGIN Includes */
#include "usart.h"
#include "lcd.h"
#include "monitor.h"
#include "settings.h"
#include "log_store.h"
#include "encoder.h"
#include "rtc_time.h"
#include "FreeRTOS.h"
#include "task.h"
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

/* USER CODE BEGIN PV */

/* USER CODE END PV */

/* Private function prototypes -----------------------------------------------*/
void SystemClock_Config(void);
/* USER CODE BEGIN PFP */

/* USER CODE END PFP */

/* Private user code ---------------------------------------------------------*/
/* USER CODE BEGIN 0 */

/* USER CODE END 0 */

/**
  * @brief  The application entry point.
  * @retval int
  */
int main(void)
{

  /* USER CODE BEGIN 1 */

  /* USER CODE END 1 */

  /* MCU Configuration--------------------------------------------------------*/

  /* Reset of all peripherals, Initializes the Flash interface and the Systick. */
  HAL_Init();

  /* USER CODE BEGIN Init */

  /* USER CODE END Init */

  /* Configure the system clock */
  SystemClock_Config();

  /* USER CODE BEGIN SysInit */

  /* USER CODE END SysInit */

  /* Initialize all configured peripherals */
  MX_GPIO_Init();
  MX_SPI1_Init();
  MX_TIM4_Init();
  MX_USART3_UART_Init();

  /* 启动 TIM4 编码器模式（MX_TIM4_Init 只配置不启动） */
  Encoder_Init();
  /* USER CODE BEGIN 2 */

  /* 串口先打印启动信息（调度器未启动，直接阻塞发送） */
  Log_Printf("\r\n[BOOT] MCU init done, starting FreeRTOS...\r\n");

  /* 点屏（调度器前调用，避免 HAL_Delay 与 tick 冲突）
   * 背景由 InputTask 的 first_run 逻辑负责刷新，这里只做硬件初始化 */
  Log_Printf("[BOOT] LCD init start\r\n");
  LCD_Init();
  Log_Printf("[BOOT] LCD init done\r\n");

  /* 加载系统设置（从 Flash 读取，掉电保持） */
  Settings_Init();
  Log_Printf("[BOOT] Settings loaded\r\n");

  /* 初始化软件时钟（从 Flash 读取上次时间） */
  RTC_Init();
  Log_Printf("[BOOT] RTC time loaded: %02d:%02d:%02d\r\n",
             RTC_GetHour(), RTC_GetMinute(), RTC_GetSecond());

  /* 初始化日志存储（环形缓冲区） */
  LogStore_Init();
  Log_Printf("[BOOT] LogStore initialized\r\n");

  /* 创建监控任务和 LED 任务 */
  Log_Printf("[BOOT] Monitor_Start\r\n");
  Monitor_Start();
  Log_Printf("[BOOT] Monitor_Start done, free_heap=%u\r\n", xPortGetFreeHeapSize());

  /* 启动调度器，永不返回 */
  Log_Printf("[BOOT] vTaskStartScheduler\r\n");

  /* 关键修复：把 SysTick 优先级设为最低
   * HAL_Init 把 SysTick 设成优先级 0（最高），但 FreeRTOS 要求它和 PendSV 一样是最低
   * 否则 SysTick 中断会抢占 PendSV，任务切换无法触发 */
  HAL_NVIC_SetPriority(SysTick_IRQn, configLIBRARY_KERNEL_INTERRUPT_PRIORITY, 0);

  vTaskStartScheduler();

  /* 如果调度器启动失败，会走到这里 */
  Log_Printf("[BOOT] !!! Scheduler start FAILED (returned) !!!\r\n");

  /* 调度器启动失败时快速闪烁 LED */
  for (;;)
  {
    HAL_GPIO_TogglePin(LED0_GPIO_Port, LED0_Pin);
    for (volatile int i = 0; i < 1000000; i++) { __NOP(); }
  }

  /* USER CODE END 2 */

  /* Infinite loop */
  /* USER CODE BEGIN WHILE */
  while (1)
  {
    /* USER CODE END WHILE */

    /* USER CODE BEGIN 3 */
  }
  /* USER CODE END 3 */
}

/**
  * @brief System Clock Configuration
  * @retval None
  */
void SystemClock_Config(void)
{
  RCC_OscInitTypeDef RCC_OscInitStruct = {0};
  RCC_ClkInitTypeDef RCC_ClkInitStruct = {0};

  __HAL_RCC_PWR_CLK_ENABLE();
  __HAL_PWR_VOLTAGESCALING_CONFIG(PWR_REGULATOR_VOLTAGE_SCALE1);

  RCC_OscInitStruct.OscillatorType = RCC_OSCILLATORTYPE_HSE;
  RCC_OscInitStruct.HSEState = RCC_HSE_ON;
  RCC_OscInitStruct.PLL.PLLState = RCC_PLL_ON;
  RCC_OscInitStruct.PLL.PLLSource = RCC_PLLSOURCE_HSE;
  RCC_OscInitStruct.PLL.PLLM = 8;
  RCC_OscInitStruct.PLL.PLLN = 336;
  RCC_OscInitStruct.PLL.PLLP = RCC_PLLP_DIV2;
  RCC_OscInitStruct.PLL.PLLQ = 7;
  if (HAL_RCC_OscConfig(&RCC_OscInitStruct) != HAL_OK)
  {
    Error_Handler();
  }

  RCC_ClkInitStruct.ClockType = RCC_CLOCKTYPE_HCLK|RCC_CLOCKTYPE_SYSCLK
                              |RCC_CLOCKTYPE_PCLK1|RCC_CLOCKTYPE_PCLK2;
  RCC_ClkInitStruct.SYSCLKSource = RCC_SYSCLKSOURCE_PLLCLK;
  RCC_ClkInitStruct.AHBCLKDivider = RCC_SYSCLK_DIV1;
  RCC_ClkInitStruct.APB1CLKDivider = RCC_HCLK_DIV4;
  RCC_ClkInitStruct.APB2CLKDivider = RCC_HCLK_DIV2;

  if (HAL_RCC_ClockConfig(&RCC_ClkInitStruct, FLASH_LATENCY_5) != HAL_OK)
  {
    Error_Handler();
  }
}

/* USER CODE BEGIN 4 */

/* USER CODE END 4 */

/**
  * @brief  This function is executed in case of error occurrence.
  * @retval None
  */
void Error_Handler(void)
{
  /* USER CODE BEGIN Error_Handler_Debug */
  __disable_irq();
  while (1)
  {
  }
  /* USER CODE END Error_Handler_Debug */
}
#ifdef USE_FULL_ASSERT
/**
  * @brief  Reports the name of the source file and the source line number
  *         where the assert_param error has occurred.
  * @param  file: pointer to the source file name
  * @param  line: assert_param error line source number
  * @retval None
  */
void assert_failed(uint8_t *file, uint32_t line)
{
  /* USER CODE BEGIN 6 */
  /* USER CODE END 6 */
}
#endif /* USE_FULL_ASSERT */
