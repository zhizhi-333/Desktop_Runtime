#ifndef DIAG_H
#define DIAG_H

/* ============================================================
 * 串口/按键诊断工具（独立模块，不依赖 Log_Printf）
 *
 * 用途：
 *   1. Diag_TestUART()      - 在 main.c MX_USART3_UART_Init 之后立即调用
 *                              直接发送测试字符，确认 USART3 硬件链路
 *   2. Diag_ReportKey(key)  - 在 InputTask 中调用，按键状态变化时报告
 *                              用于诊断 OK 键不灵敏问题
 *   3. Diag_Tick()          - 每秒发送心跳，确认调度器运行
 *
 * 设计原则：
 *   - 独立文件，不修改 Log_Printf / Key_Scan 等已实现功能
 *   - 直接调 HAL_UART_Transmit，绕过 Log_Printf 的 mutex 逻辑
 *     （排除 Log_Printf 自身 bug 的可能）
 *   - 调用点仅 1 行，对 main.c / monitor.c 改动最小
 * ============================================================ */

#include <stdint.h>
#include "key.h"

/* 串口硬件链路测试：发送固定字符串，确认 PD8 TX 是否工作 */
void Diag_TestUART(void);

/* 按键状态报告：仅在按键状态变化时发送，避免刷屏 */
void Diag_ReportKey(const key_state_t *key);

/* 心跳：每秒发送一次，确认调度器和串口都在跑 */
void Diag_Tick(void);

#endif /* DIAG_H */
