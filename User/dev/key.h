#ifndef KEY_H
#define KEY_H

#include "main.h"

/* 4x4 矩阵键盘行线（推挽输出） */
#define KEY_ROW0_Pin        GPIO_PIN_0
#define KEY_ROW0_GPIO_Port  GPIOE
#define KEY_ROW1_Pin        GPIO_PIN_1
#define KEY_ROW1_GPIO_Port  GPIOE
#define KEY_ROW2_Pin        GPIO_PIN_2
#define KEY_ROW2_GPIO_Port  GPIOE
#define KEY_ROW3_Pin        GPIO_PIN_3
#define KEY_ROW3_GPIO_Port  GPIOE

/* 4x4 矩阵键盘列线（输入上拉） */
#define KEY_COL0_Pin        GPIO_PIN_4
#define KEY_COL0_GPIO_Port  GPIOE
#define KEY_COL1_Pin        GPIO_PIN_5
#define KEY_COL1_GPIO_Port  GPIOE
#define KEY_COL2_Pin        GPIO_PIN_7
#define KEY_COL2_GPIO_Port  GPIOE
#define KEY_COL3_Pin        GPIO_PIN_8
#define KEY_COL3_GPIO_Port  GPIOE

/* 输入器 ID 检测脚 */
#define KEY_IDA_Pin         GPIO_PIN_6
#define KEY_IDA_GPIO_Port   GPIOE

/* 编码器开关 */
#define KEY_ECSW_Pin        GPIO_PIN_8
#define KEY_ECSW_GPIO_Port  GPIOB

#define KEY_PRESSED     0
#define KEY_RELEASED    1

typedef struct {
    uint8_t up;
    uint8_t down;
    uint8_t left;
    uint8_t right;
    uint8_t ok;
    uint8_t back;
    uint8_t ec_sw;
    uint8_t id_connected;
} key_state_t;

void Key_Init(void);
void Key_Scan(key_state_t *state);

#endif
