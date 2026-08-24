#ifndef DS1302_H
#define DS1302_H

#include <stdint.h>

/* ============================================================
 * DS1302 实时时钟模块驱动
 *
 * 硬件：DS1302 + 32.768kHz 晶振 + 后备电池 (CR2032)
 * 接线：CLK→PC0, DAT→PC1, RST→PC2, VCC→3.3V, GND→GND
 *
 * 原理：
 *   - 3 线串行接口 (CE/IO/SCLK)
 *   - 数据以 BCD 格式存储
 *   - 主控掉电后由模块自带电池维持走时
 *   - 重新上电直接读取模块寄存器获取准确时间
 *
 * 备注：DS1302 走时精度依赖外部 32.768kHz 晶振，约 ±20ppm
 *       （每月约 1 分钟误差），与原 STM32 内置 RTC 方案精度相当
 * ============================================================ */

/* 初始化 DS1302：配置 GPIO + 关写保护 + 关涓流充电 + 检测停摆 */
void DS1302_Init(void);

/* 读取时间（24h 制，BCD→BIN 转换在驱动内完成）
 * 任意参数可传 NULL 表示不读取该字段 */
void DS1302_ReadTime(uint8_t *hour, uint8_t *min, uint8_t *sec);

/* 写入时间（24h 制，参数会自动 BIN→BCD） */
void DS1302_WriteTime(uint8_t hour, uint8_t min, uint8_t sec);

/* 检测是否停摆：返回 1=停摆(首次上电或电池耗尽), 0=走时中 */
uint8_t DS1302_IsHalted(void);

#endif /* DS1302_H */
