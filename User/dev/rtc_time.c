#include "rtc_time.h"
#include "stm32f4xx_hal.h"
#include "usart.h"
#include <string.h>

/* ============================================================
 * 硬件 RTC 实现 (STM32F407 内置 RTC + LSE 32.768kHz 晶振)
 *
 * 原理：
 *   - LSE 外部 32.768kHz 晶振作为 RTC 时钟源（VBAT 供电）
 *   - 掉电后 VBAT 纽扣电池维持 RTC 走时
 *   - 开机直接读取 RTC 日历寄存器获取准确时间
 *   - 使用 24 小时制 BCD 格式
 *
 * 初始化策略：
 *   - 首次烧录：LSE 启动 + RTC 初始化 + 默认时间 00:00:00
 *   - 后续开机：RTC 已初始化（INITS 标志置位），直接读取时间
 * ============================================================ */

#define RTC_FORMAT      RTC_FORMAT_BIN   /* 使用 BIN 格式，避免 BCD 转换 */

static RTC_HandleTypeDef hrtc;
static int rtc_initialized = 0;
static int rtc_clk_src = 0;   /* 0=未知 1=LSE 2=LSI */

/* ---- 初始化 RTC 外设 ---- */
static void rtc_hw_init(void)
{
    RCC_OscInitTypeDef osc = {0};
    RCC_PeriphCLKInitTypeDef periph = {0};

    /* 1. 配置 LSE 外部 32.768kHz 晶振 */
    osc.OscillatorType = RCC_OSCILLATORTYPE_LSE;
    osc.LSEState = RCC_LSE_ON;
    if (HAL_RCC_OscConfig(&osc) != HAL_OK)
    {
        /* LSE 启动失败，可能是没有外部晶振，回退到 LSI */
        Log_Printf("[RTC] LSE start FAILED (no 32.768kHz crystal?), fallback to LSI\r\n");
        Log_Printf("[RTC] WARNING: LSI cannot keep time during power-off!\r\n");

        osc.OscillatorType = RCC_OSCILLATORTYPE_LSI;
        osc.LSEState = RCC_LSE_OFF;
        osc.LSIState = RCC_LSI_ON;
        HAL_RCC_OscConfig(&osc);

        periph.PeriphClockSelection = RCC_PERIPHCLK_RTC;
        periph.RTCClockSelection = RCC_RTCCLKSOURCE_LSI;
        rtc_clk_src = 2;
    }
    else
    {
        Log_Printf("[RTC] LSE start OK (32.768kHz)\r\n");
        periph.PeriphClockSelection = RCC_PERIPHCLK_RTC;
        periph.RTCClockSelection = RCC_RTCCLKSOURCE_LSE;
        rtc_clk_src = 1;
    }
    HAL_RCCEx_PeriphCLKConfig(&periph);

    /* 读取 RCC 备份寄存器状态，判断 VBAT 是否曾掉电 */
    {
        uint32_t bdcr = RCC->BDCR;
        Log_Printf("[RTC] BDCR=0x%08X (LSEON=%d LSERDY=%d RTCSEL=%d RTCEN=%d)\r\n",
                   (unsigned)bdcr,
                   (int)((bdcr >> RCC_BDCR_LSEON_Pos) & 1),
                   (int)((bdcr >> RCC_BDCR_LSERDY_Pos) & 1),
                   (int)((bdcr >> RCC_BDCR_RTCSEL_Pos) & 3),
                   (int)((bdcr >> RCC_BDCR_RTCEN_Pos) & 1));
    }

    /* 2. 使能 RTC 时钟 */
    __HAL_RCC_RTC_ENABLE();

    hrtc.Instance = RTC;
    hrtc.Init.HourFormat = RTC_HOURFORMAT_24;
    hrtc.Init.AsynchPrediv = 0x7F;   /* LSE: 32768/128 = 256Hz */
    hrtc.Init.SynchPrediv = 0xFF;    /* 256Hz/256 = 1Hz */
    hrtc.Init.OutPut = RTC_OUTPUT_DISABLE;
    hrtc.Init.OutPutPolarity = RTC_OUTPUT_POLARITY_HIGH;
    hrtc.Init.OutPutType = RTC_OUTPUT_TYPE_OPENDRAIN;

    /* 3. 检查 RTC 是否已初始化（判断是否首次上电）
     * 本版本 HAL 库用 __HAL_RTC_IS_CALENDAR_INITIALIZED 替代 __HAL_RTC_GET_FLAG
     * INITS 标志在 VBAT 维持时会保持；VBAT 掉电或首次上电时为 0 */
    if (__HAL_RTC_IS_CALENDAR_INITIALIZED(&hrtc) == 0)
    {
        /* 首次上电：初始化 RTC 并设置默认时间 00:00:00 */
        Log_Printf("[RTC] first power-on (INITS=0), setting default 00:00:00\r\n");
        Log_Printf("[RTC] HINT: if VBAT has battery and LSE OK, next power-on should preserve time\r\n");
        HAL_RTC_Init(&hrtc);
        RTC_TimeTypeDef time = {0};
        time.Hours = 0;
        time.Minutes = 0;
        time.Seconds = 0;
        time.TimeFormat = RTC_HOURFORMAT12_AM;
        time.DayLightSaving = RTC_DAYLIGHTSAVING_NONE;
        time.StoreOperation = RTC_STOREOPERATION_RESET;
        HAL_RTC_SetTime(&hrtc, &time, RTC_FORMAT);
    }
    else
    {
        /* 非首次：RTC 已在运行（VBAT 维持），只需初始化句柄 */
        RTC_TimeTypeDef time = {0};
        RTC_DateTypeDef date = {0};
        HAL_RTC_Init(&hrtc);
        HAL_RTC_GetTime(&hrtc, &time, RTC_FORMAT);
        HAL_RTC_GetDate(&hrtc, &date, RTC_FORMAT);
        Log_Printf("[RTC] INITS=1, time preserved: %02d:%02d:%02d\r\n",
                   time.Hours, time.Minutes, time.Seconds);
    }

    Log_Printf("[RTC] clk_src=%s\r\n", rtc_clk_src == 1 ? "LSE" : "LSI");
    if (rtc_clk_src == 2)
        Log_Printf("[RTC] WARNING: using LSI, time will NOT survive power-off!\r\n");
    else
        Log_Printf("[RTC] LSE OK, but VBAT battery needed for power-off retention\r\n");
}

void RTC_Init(void)
{
    if (!rtc_initialized)
    {
        rtc_hw_init();
        rtc_initialized = 1;
    }
}

uint8_t RTC_GetHour(void)
{
    RTC_TimeTypeDef time;
    if (!rtc_initialized) return 0;
    /* 注意：必须先读 Date 才能锁存 Time 寄存器（STM32F4 RTC 硬件要求） */
    RTC_DateTypeDef date;
    HAL_RTC_GetTime(&hrtc, &time, RTC_FORMAT);
    HAL_RTC_GetDate(&hrtc, &date, RTC_FORMAT);
    return (uint8_t)time.Hours;
}

uint8_t RTC_GetMinute(void)
{
    RTC_TimeTypeDef time;
    if (!rtc_initialized) return 0;
    RTC_DateTypeDef date;
    HAL_RTC_GetTime(&hrtc, &time, RTC_FORMAT);
    HAL_RTC_GetDate(&hrtc, &date, RTC_FORMAT);
    return (uint8_t)time.Minutes;
}

uint8_t RTC_GetSecond(void)
{
    RTC_TimeTypeDef time;
    if (!rtc_initialized) return 0;
    RTC_DateTypeDef date;
    HAL_RTC_GetTime(&hrtc, &time, RTC_FORMAT);
    HAL_RTC_GetDate(&hrtc, &date, RTC_FORMAT);
    return (uint8_t)time.Seconds;
}

void RTC_SetTime(uint8_t hour, uint8_t minute, uint8_t second)
{
    RTC_TimeTypeDef time;
    if (!rtc_initialized) return;

    if (hour >= 24) hour = 23;
    if (minute >= 60) minute = 59;
    if (second >= 60) second = 59;

    time.Hours = hour;
    time.Minutes = minute;
    time.Seconds = second;
    time.TimeFormat = RTC_HOURFORMAT12_AM;
    time.DayLightSaving = RTC_DAYLIGHTSAVING_NONE;
    time.StoreOperation = RTC_STOREOPERATION_RESET;

    /* 关闭写保护后写入 */
    HAL_RTC_SetTime(&hrtc, &time, RTC_FORMAT);
}

/* 硬件 RTC 由 VBAT 维持，不需要 Flash 保存 */
void RTC_Save(void)
{
    /* 空实现：硬件 RTC 自动保持，无需保存 */
}
