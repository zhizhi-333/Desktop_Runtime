#include "encoder.h"
#include "stm32f4xx_hal.h"
#include "tim.h"

static uint16_t last_cnt;

void Encoder_Init(void)
{
    /* MX_TIM4_Init() 已经完成了编码器模式的配置（HAL_TIM_Encoder_Init），
     * 这里只需启动定时器并记录初始计数值 */
    HAL_TIM_Encoder_Start(&htim4, TIM_CHANNEL_ALL);
    last_cnt = __HAL_TIM_GET_COUNTER(&htim4);
}

int16_t Encoder_GetDelta(void)
{
    uint16_t cnt = __HAL_TIM_GET_COUNTER(&htim4);
    int16_t delta = (int16_t)(cnt - last_cnt);
    last_cnt = cnt;
    if (delta > -2 && delta < 2) return 0;
    return delta;
}
