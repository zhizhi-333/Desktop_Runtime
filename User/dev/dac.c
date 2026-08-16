#include "dac.h"
#include "stm32f4xx_hal.h"
#include "usart.h"
#include "FreeRTOS.h"
#include "task.h"
#include "semphr.h"

/* ============================================================
 * DAC1_CH1 (PA4) 驱动实现
 *
 * 原理:
 *   - DAC 12 位 (0~4095), 输出电压 = VDD * value / 4095
 *   - TIM6 定时触发 DAC 输出, 产生方波
 *   - 方波频率 = TIM6 触发频率 / 2 (每次触发翻转电平)
 *
 * 简化实现:
 *   - 用 TIM6 中断回调翻转 DAC 输出值 (高/低电平)
 *   - 音量通过调整高电平幅度实现 (0~2047)
 *
 * 同步保护: 互斥量 dac_mutex
 *   - MusicTask 调用 DAC_Start/Stop/SetFreq
 *   - app_music 读取 DAC_GetVolume/IsPlaying
 *   - TIM6 中断读取 playing/volume/toggle
 *   - mutex 保护任务侧访问,中断侧读 volatile 变量(无需锁)
 * ============================================================ */

static DAC_HandleTypeDef hdac;
static TIM_HandleTypeDef htim6;
static volatile uint8_t volume = 50;          /* 当前音量 0~100 (volatile: 中断读取) */
static volatile int playing = 0;              /* 是否在播放 (volatile: 中断读取) */
static uint8_t toggle = 0;                    /* 方波翻转标志 */
static volatile uint32_t dac_isr_count = 0;   /* TIM6 中断计数(诊断用) */
static SemaphoreHandle_t dac_mutex = NULL;    /* 任务侧互斥量 */

/* ---- TIM6 中断回调 ---- */
void HAL_TIM_PeriodElapsedCallback(TIM_HandleTypeDef *htim)
{
    if (htim->Instance == TIM6)
    {
        if (playing)
        {
            uint16_t amp = (uint16_t)(2047UL * volume / 100);  /* 0~2047 */
            if (toggle)
                HAL_DAC_SetValue(&hdac, DAC_CHANNEL_1, DAC_ALIGN_12B_R, 2048 + amp);
            else
                HAL_DAC_SetValue(&hdac, DAC_CHANNEL_1, DAC_ALIGN_12B_R, 2048 - amp);
            toggle = !toggle;
        }
        dac_isr_count++;
    }
}

/* ---- 初始化 GPIO PA4 为模拟输出 ---- */
static void dac_gpio_init(void)
{
    GPIO_InitTypeDef gpio = {0};

    __HAL_RCC_GPIOA_CLK_ENABLE();

    gpio.Pin = GPIO_PIN_4;
    gpio.Mode = GPIO_MODE_ANALOG;
    gpio.Pull = GPIO_NOPULL;
    HAL_GPIO_Init(GPIOA, &gpio);
}

/* ---- 初始化 TIM6 (用于触发 DAC) ---- */
static void tim6_init(void)
{
    __HAL_RCC_TIM6_CLK_ENABLE();

    htim6.Instance = TIM6;
    /* 预分频 83: 定时器时钟 = 84MHz/(83+1) = 1MHz
     * 用 1MHz 是为了让低频音符的 ARR 落在 16 位内:
     *   arr = 1MHz / (2*freq): 131Hz->3816, 262Hz->1908, 440Hz->1136, 1047Hz->477
     * 原来 Prescaler=0(84MHz) 时, 低于 ~641Hz 的音符 arr>65535 被钳位,
     * 导致旋律里所有音符都变成同一个 ~641Hz 单调蜂鸣(有声音没旋律) */
    htim6.Init.Prescaler = 83;
    htim6.Init.CounterMode = TIM_COUNTERMODE_UP;
    htim6.Init.Period = 1000;        /* 默认值, SetFreq 会修改 */
    htim6.Init.ClockDivision = TIM_CLOCKDIVISION_DIV1;
    htim6.Init.AutoReloadPreload = TIM_AUTORELOAD_PRELOAD_ENABLE;
    HAL_TIM_Base_Init(&htim6);

    HAL_NVIC_SetPriority(TIM6_DAC_IRQn, 0x0F, 0);
    HAL_NVIC_EnableIRQ(TIM6_DAC_IRQn);
}

/* ---- 初始化 DAC1_CH1 ---- */
void DAC_Init(void)
{
    DAC_ChannelConfTypeDef sConfig = {0};

    Log_Printf("[DAC] DAC_Init start\r\n");

    /* 创建互斥量(调度器可能未启动,但 xSemaphoreCreateMutex 在启动前也可用) */
    if (dac_mutex == NULL)
        dac_mutex = xSemaphoreCreateMutex();

    dac_gpio_init();
    Log_Printf("[DAC] GPIO PA4 configured (analog)\r\n");

    __HAL_RCC_DAC_CLK_ENABLE();
    Log_Printf("[DAC] DAC clock enabled\r\n");

    hdac.Instance = DAC;
    if (HAL_DAC_Init(&hdac) != HAL_OK)
    {
        Log_Printf("[DAC] !!! HAL_DAC_Init FAILED !!!\r\n");
        return;
    }
    Log_Printf("[DAC] HAL_DAC_Init OK\r\n");

    sConfig.DAC_Trigger = DAC_TRIGGER_NONE;   /* 软件触发 */
    sConfig.DAC_OutputBuffer = DAC_OUTPUTBUFFER_ENABLE;
    HAL_DAC_ConfigChannel(&hdac, &sConfig, DAC_CHANNEL_1);
    Log_Printf("[DAC] Channel 1 configured (no trigger, buffered)\r\n");

    /* 初始输出中点电压 (静音) */
    HAL_DAC_SetValue(&hdac, DAC_CHANNEL_1, DAC_ALIGN_12B_R, 2048);
    HAL_DAC_Start(&hdac, DAC_CHANNEL_1);
    Log_Printf("[DAC] DAC started, output=2048 (midpoint)\r\n");

    tim6_init();
    Log_Printf("[DAC] TIM6 initialized for audio trigger\r\n");
    Log_Printf("[DAC] DAC_Init complete. PA4 should read ~1.65V\r\n");
}

/* ---- 设置输出频率 ---- */
void DAC_SetFreq(uint32_t freq)
{
    uint32_t arr;

    if (freq == 0)
    {
        DAC_Stop();
        return;
    }

    /* 方波频率 = TIM6 触发频率 / 2
     * TIM6 定时器时钟 = 84MHz / (Prescaler+1) = 84MHz/84 = 1MHz (见 tim6_init)
     * 触发频率 = 2 * freq, 故 ARR = 1MHz / (2 * freq)
     * 1MHz 下所有音符(131~1047Hz)的 ARR(477~3816)都在 16 位内, 不再被钳位 */
    if (freq < 100) freq = 100;
    if (freq > 5000) freq = 5000;

    arr = 1000000UL / (2 * freq);
    if (arr == 0) arr = 1;
    if (arr > 65535) arr = 65535;   /* 安全兜底: 1MHz 下实际不会触发 */

    /* ARR 寄存器写入是原子的,且 TIM6 有 preload,无需加锁 */
    __HAL_TIM_SET_AUTORELOAD(&htim6, arr - 1);
}

/* ---- 开始播放 ---- */
void DAC_Start(void)
{
    if (xTaskGetSchedulerState() == taskSCHEDULER_NOT_STARTED || dac_mutex == NULL)
    {
        if (!playing)
        {
            playing = 1;
            toggle = 0;
            dac_isr_count = 0;
            HAL_TIM_Base_Start_IT(&htim6);
        }
        return;
    }

    if (xSemaphoreTake(dac_mutex, pdMS_TO_TICKS(50)) == pdTRUE)
    {
        if (!playing)
        {
            playing = 1;
            toggle = 0;
            dac_isr_count = 0;
            HAL_TIM_Base_Start_IT(&htim6);
            Log_Printf("[DAC] DAC_Start: TIM6 interrupt started\r\n");
        }
        xSemaphoreGive(dac_mutex);
    }
}

/* ---- 停止播放 ---- */
void DAC_Stop(void)
{
    if (xTaskGetSchedulerState() == taskSCHEDULER_NOT_STARTED || dac_mutex == NULL)
    {
        if (playing)
        {
            playing = 0;
            HAL_TIM_Base_Stop_IT(&htim6);
            HAL_DAC_SetValue(&hdac, DAC_CHANNEL_1, DAC_ALIGN_12B_R, 2048);
        }
        return;
    }

    if (xSemaphoreTake(dac_mutex, pdMS_TO_TICKS(50)) == pdTRUE)
    {
        if (playing)
        {
            playing = 0;
            HAL_TIM_Base_Stop_IT(&htim6);
            HAL_DAC_SetValue(&hdac, DAC_CHANNEL_1, DAC_ALIGN_12B_R, 2048);  /* 静音 */
            Log_Printf("[DAC] DAC_Stop: ISR fired %u times\r\n", dac_isr_count);
        }
        xSemaphoreGive(dac_mutex);
    }
}

/* ---- 设置音量 ---- */
void DAC_SetVolume(uint8_t vol)
{
    if (vol > 100) vol = 100;
    /* volume 是 volatile uint8_t,单字节写入原子,无需加锁 */
    volume = vol;
}

/* ---- 获取音量 ---- */
uint8_t DAC_GetVolume(void)
{
    return volume;
}

/* ---- 是否在播放 ---- */
int DAC_IsPlaying(void)
{
    return playing;
}

/* ---- 获取 TIM6 中断计数(诊断用) ---- */
uint32_t DAC_GetIsrCount(void)
{
    return dac_isr_count;
}

/* ---- TIM6/DAC 中断处理 ---- */
void TIM6_DAC_IRQHandler(void)
{
    HAL_TIM_IRQHandler(&htim6);
}
