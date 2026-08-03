#ifndef RTC_TIME_H
#define RTC_TIME_H

#include <stdint.h>

/* ============================================================
 * 硬件 RTC 接口 (STM32F407 内置 RTC + VBAT 纽扣电池)
 *
 * 使用 LSE 32.768kHz 外部晶振，掉电后由 VBAT 维持走时
 * 开机直接读取 RTC 日历寄存器获取准确时间
 * ============================================================ */

/* 初始化硬件 RTC（首次上电设默认时间，后续读取 VBAT 维持的时间） */
void RTC_Init(void);

/* 获取当前时间 */
uint8_t RTC_GetHour(void);
uint8_t RTC_GetMinute(void);
uint8_t RTC_GetSecond(void);

/* 设置当前时间（写入 RTC 寄存器，VBAT 维持） */
void RTC_SetTime(uint8_t hour, uint8_t minute, uint8_t second);

/* 保存时间（硬件 RTC 自动保持，此函数为空实现，保留接口兼容） */
void RTC_Save(void);

#endif /* RTC_TIME_H */
