#include "key.h"
#include "stm32f4xx_hal.h"
#include <string.h>

static key_state_t prev_raw, debounced;
static uint8_t stable_cnt[8];

#define STABLE_THR     3

#define ROW_PINS   (KEY_ROW0_Pin | KEY_ROW1_Pin | KEY_ROW2_Pin | KEY_ROW3_Pin)

static void set_all_rows_high(void)
{
    HAL_GPIO_WritePin(KEY_ROW0_GPIO_Port, ROW_PINS, GPIO_PIN_SET);
}

/* 4x4 矩阵扫描，填充 raw 结构体
 * 按键位置映射：
 *         C0(PE4)  C1(PE5)  C2(PE7)  C3(PE8)
 * R0(PE0)  上       -        -        左
 * R1(PE1)  -        确认     -        -
 * R2(PE2)  -        -        返回     -
 * R3(PE3)  下       -        -        右
 */
static void scan_matrix(key_state_t *raw)
{
    const uint16_t row_pin[4] = { KEY_ROW0_Pin, KEY_ROW1_Pin, KEY_ROW2_Pin, KEY_ROW3_Pin };
    uint8_t c0, c1, c2, c3;
    int r;

    raw->up = raw->down = raw->left = raw->right = raw->ok = raw->back = 0;

    for (r = 0; r < 4; r++)
    {
        /* 所有行拉高，再拉低当前行 */
        set_all_rows_high();
        HAL_GPIO_WritePin(KEY_ROW0_GPIO_Port, row_pin[r], GPIO_PIN_RESET);

        /* 短延时等电平稳定 */
        volatile int d;
        for (d = 0; d < 200; d++);

        c0 = (HAL_GPIO_ReadPin(KEY_COL0_GPIO_Port, KEY_COL0_Pin) == GPIO_PIN_RESET) ? 1 : 0;
        c1 = (HAL_GPIO_ReadPin(KEY_COL1_GPIO_Port, KEY_COL1_Pin) == GPIO_PIN_RESET) ? 1 : 0;
        c2 = (HAL_GPIO_ReadPin(KEY_COL2_GPIO_Port, KEY_COL2_Pin) == GPIO_PIN_RESET) ? 1 : 0;
        c3 = (HAL_GPIO_ReadPin(KEY_COL3_GPIO_Port, KEY_COL3_Pin) == GPIO_PIN_RESET) ? 1 : 0;

        if (r == 0) { if (c0) raw->up   = 1; if (c3) raw->left  = 1; }
        if (r == 1) { if (c1) raw->ok   = 1; }
        if (r == 2) { if (c2) raw->back = 1; }
        if (r == 3) { if (c0) raw->down = 1; if (c3) raw->right = 1; }
    }

    /* 扫描结束，所有行拉高，降低待机电流 */
    set_all_rows_high();

    /* 编码器开关与输入器 ID（非矩阵，直接读取） */
    raw->ec_sw = (HAL_GPIO_ReadPin(KEY_ECSW_GPIO_Port, KEY_ECSW_Pin) == GPIO_PIN_RESET) ? 1 : 0;
    /* ID 检测：低电平=已连接，高电平=未连接 */
    raw->id_connected = (HAL_GPIO_ReadPin(KEY_IDA_GPIO_Port, KEY_IDA_Pin) == GPIO_PIN_RESET) ? 1 : 0;
}

void Key_Init(void)
{
    GPIO_InitTypeDef gpio = {0};

    __HAL_RCC_GPIOE_CLK_ENABLE();
    __HAL_RCC_GPIOB_CLK_ENABLE();

    /* 行线：推挽输出，默认高电平 */
    gpio.Mode = GPIO_MODE_OUTPUT_PP;
    gpio.Pull = GPIO_NOPULL;
    gpio.Speed = GPIO_SPEED_FREQ_LOW;
    gpio.Pin = KEY_ROW0_Pin | KEY_ROW1_Pin | KEY_ROW2_Pin | KEY_ROW3_Pin;
    HAL_GPIO_Init(GPIOE, &gpio);
    set_all_rows_high();

    /* 列线 + ID：输入上拉 */
    gpio.Mode = GPIO_MODE_INPUT;
    gpio.Pull = GPIO_PULLUP;
    gpio.Speed = GPIO_SPEED_FREQ_LOW;
    gpio.Pin = KEY_COL0_Pin | KEY_COL1_Pin | KEY_COL2_Pin | KEY_COL3_Pin | KEY_IDA_Pin;
    HAL_GPIO_Init(GPIOE, &gpio);

    /* 编码器开关：输入上拉 */
    gpio.Pin = KEY_ECSW_Pin;
    HAL_GPIO_Init(GPIOB, &gpio);

    memset(&prev_raw, 0, sizeof(prev_raw));
    memset(&debounced, 0, sizeof(debounced));
    memset(stable_cnt, 0, sizeof(stable_cnt));
}

void Key_Scan(key_state_t *state)
{
    key_state_t raw;
    uint8_t *r = (uint8_t*)&raw;
    uint8_t *p = (uint8_t*)&prev_raw;
    uint8_t *d = (uint8_t*)&debounced;
    int i;

    scan_matrix(&raw);

    for (i = 0; i < 8; i++)
    {
        if (r[i] == p[i])
        {
            if (stable_cnt[i] < STABLE_THR) stable_cnt[i]++;
        }
        else
        {
            stable_cnt[i] = 0;
        }

        if (stable_cnt[i] >= STABLE_THR) d[i] = r[i];
        ((uint8_t*)state)[i] = d[i];
        p[i] = r[i];
    }
}
