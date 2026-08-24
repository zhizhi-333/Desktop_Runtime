#include "rtc_time.h"
#include "ds1302.h"
#include "usart.h"

/* ============================================================
 * 实时时钟适配层 (基于外部 DS1302 模块)
 *
 * 设计：
 *   - DS1302 模块自带电池（CR2032），主控掉电后维持走时
 *   - 重新上电直接读取 DS1302 寄存器，无需重新设时
 *   - 本文件对外保持 RTC_Init / RTC_GetHour / RTC_SetTime 等 API 不变
 *     上层模块（login/desktop/settime/monitor）无需任何修改
 *
 * 历史背景：
 *   - 原方案用 STM32F407 内置 RTC + VBAT 纽扣电池 + LSE 32.768kHz 晶振
 *   - 实测：核心板 VBAT 焊接电池后重新上电时间仍从 0 开始
 *     推测原因：VBAT 引脚焊接问题 / LSE 晶振未启动 / 内部 RTC 配置问题
 *   - 改用 DS1302 外部模块：硬件独立、走时不依赖主控 VBAT、调试直观
 *
 * 错误处理：
 *   - 首次上电（DS1302 停摆 CH=1）设默认 00:00:00 并启动走时
 *   - 上层 monitor.c 通过 SYS_ERR_RTC 标志提醒用户去 SETTIME 设置时间
 * ============================================================ */

static int rtc_initialized = 0;
int rtc_first_power_on = 0;  /* 1=首次上电(DS1302 停摆)，供外部检测 */

void RTC_Init(void)
{
    if (rtc_initialized) return;

    DS1302_Init();

    if (DS1302_IsHalted())
    {
        /* DS1302 停摆：首次上电 / 电池耗尽 / 模块刚装上电池 */
        rtc_first_power_on = 1;
        Log_Printf("[RTC] DS1302 halted, setting default 00:00:00 and starting...\r\n");
        DS1302_WriteTime(0, 0, 0);   /* 清除 CH 位并设默认时间 */
        /* 保留 rtc_first_power_on=1：让 monitor.c 设置 SYS_ERR_RTC 标志
         * 提示用户进 SETTIME 应用设置实际时间，设置完成会清错误标志 */
    }
    else
    {
        /* DS1302 走时中：电池维持有效，直接读时间 */
        uint8_t h, m, s;
        DS1302_ReadTime(&h, &m, &s);
        Log_Printf("[RTC] DS1302 running, time preserved: %02d:%02d:%02d\r\n", h, m, s);
    }

    rtc_initialized = 1;
}

uint8_t RTC_GetHour(void)
{
    uint8_t h, m, s;
    if (!rtc_initialized) return 0;
    DS1302_ReadTime(&h, &m, &s);
    return h;
}

uint8_t RTC_GetMinute(void)
{
    uint8_t h, m, s;
    if (!rtc_initialized) return 0;
    DS1302_ReadTime(&h, &m, &s);
    return m;
}

uint8_t RTC_GetSecond(void)
{
    uint8_t h, m, s;
    if (!rtc_initialized) return 0;
    DS1302_ReadTime(&h, &m, &s);
    return s;
}

void RTC_SetTime(uint8_t hour, uint8_t minute, uint8_t second)
{
    if (!rtc_initialized) return;
    DS1302_WriteTime(hour, minute, second);
}

/* DS1302 自带电池维持走时，无需 Flash 保存（保留接口兼容） */
void RTC_Save(void)
{
    /* 空实现：DS1302 模块自动保持 */
}
