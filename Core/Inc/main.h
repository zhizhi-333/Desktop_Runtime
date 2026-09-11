/* USER CODE BEGIN Header */
/**
  ******************************************************************************
  * @file           : main.h
  * @brief          : Header for main.c file.
  *                   This file contains the common defines of the application.
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

/* Define to prevent recursive inclusion -------------------------------------*/
#ifndef __MAIN_H
#define __MAIN_H

#ifdef __cplusplus
extern "C" {
#endif

/* Includes ------------------------------------------------------------------*/
#include "stm32f4xx_hal.h"

/* Private includes ----------------------------------------------------------*/
/* USER CODE BEGIN Includes */

/* USER CODE END Includes */

/* Exported types ------------------------------------------------------------*/
/* USER CODE BEGIN ET */

/* USER CODE END ET */

/* Exported constants --------------------------------------------------------*/
/* USER CODE BEGIN EC */

/* USER CODE END EC */

/* Exported macro ------------------------------------------------------------*/
/* USER CODE BEGIN EM */

/* USER CODE END EM */

/* Exported functions prototypes ---------------------------------------------*/
void Error_Handler(void);

/* USER CODE BEGIN EFP */

/* USER CODE END EFP */

/* Private defines -----------------------------------------------------------*/
/* 4x4 矩阵键盘行线（推挽输出） */
#define KEY_ROW0_Pin GPIO_PIN_0
#define KEY_ROW0_GPIO_Port GPIOE
#define KEY_ROW1_Pin GPIO_PIN_1
#define KEY_ROW1_GPIO_Port GPIOE
#define KEY_ROW2_Pin GPIO_PIN_2
#define KEY_ROW2_GPIO_Port GPIOE
#define KEY_ROW3_Pin GPIO_PIN_3
#define KEY_ROW3_GPIO_Port GPIOE
/* 4x4 矩阵键盘列线（输入上拉） */
#define KEY_COL0_Pin GPIO_PIN_4
#define KEY_COL0_GPIO_Port GPIOE
#define KEY_COL1_Pin GPIO_PIN_5
#define KEY_COL1_GPIO_Port GPIOE
#define KEY_COL2_Pin GPIO_PIN_7
#define KEY_COL2_GPIO_Port GPIOE
#define KEY_COL3_Pin GPIO_PIN_8
#define KEY_COL3_GPIO_Port GPIOE
/* 输入器 ID 检测脚 */
#define KEY_IDA_Pin GPIO_PIN_6
#define KEY_IDA_GPIO_Port GPIOE
/* LCD 控制脚 */
#define LCD_CS_Pin GPIO_PIN_12
#define LCD_CS_GPIO_Port GPIOB
#define LCD_DC_Pin GPIO_PIN_13
#define LCD_DC_GPIO_Port GPIOB
#define LCD_RST_Pin GPIO_PIN_14
#define LCD_RST_GPIO_Port GPIOB
#define LCD_BL_Pin GPIO_PIN_15
#define LCD_BL_GPIO_Port GPIOB
/* 编码器 */
#define ECN_A_Pin GPIO_PIN_6
#define ECN_A_GPIO_Port GPIOB
#define ECN_B_Pin GPIO_PIN_7
#define ECN_B_GPIO_Port GPIOB
#define ECN_SW_Pin GPIO_PIN_8
#define ECN_SW_GPIO_Port GPIOB
/* 板载 LED（PC13） */
#define LED0_Pin GPIO_PIN_13
#define LED0_GPIO_Port GPIOC

/* USER CODE BEGIN Private defines */

/* USER CODE END Private defines */

#ifdef __cplusplus
}
#endif

#endif /* __MAIN_H */
