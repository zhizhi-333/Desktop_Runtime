#ifndef DAC_H
#define DAC_H

#include "main.h"

/* ============================================================
 * DAC1 通道 1 (PA4) 驱动 - 用于音频输出
 *
 * 用途: 输出模拟电压驱动功放模块发声
 * 原理: DAC 12 位 (0~4095), 配合定时器触发产生方波/正弦波
 *
 * 使用方式:
 *   1. DAC_Init()           初始化 DAC
 *   2. DAC_SetFreq(freq)    设置输出频率
 *   3. DAC_Start()          开始输出
 *   4. DAC_Stop()           停止输出
 *   5. DAC_SetVolume(vol)   设置音量 (0~100)
 * ============================================================ */

/* 初始化 DAC1_CH1 (PA4) + TIM6 触发
 * TIM6 用于定时触发 DAC 输出，产生指定频率的方波 */
void DAC_Init(void);

/* 设置输出频率 (Hz)
 * freq=0 表示停止输出（静音）
 * 范围: 100~5000 Hz 适合蜂鸣器/小喇叭 */
void DAC_SetFreq(uint32_t freq);

/* 开始输出当前频率 */
void DAC_Start(void);

/* 停止输出（静音，DAC 输出 0） */
void DAC_Stop(void);

/* 设置音量 (0~100)
 * 通过调整 DAC 输出幅度实现
 * 0=静音, 100=最大 */
void DAC_SetVolume(uint8_t vol);

/* 获取当前音量 */
uint8_t DAC_GetVolume(void);

/* 当前是否在发声 */
int DAC_IsPlaying(void);

/* 获取 TIM6 中断计数(诊断用, 判断中断是否在跑) */
uint32_t DAC_GetIsrCount(void);

#endif /* DAC_H */
