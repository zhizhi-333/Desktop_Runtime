#include "diag.h"
#include "stm32f4xx_hal.h"
#include "usart.h"
#include <string.h>
#include <stdio.h>

/* 直接发送字符串，绕过 Log_Printf（排除 Log_Printf 自身 bug 的可能） */
static void diag_send(const char *s)
{
    HAL_UART_Transmit(&huart3, (uint8_t*)s, (uint16_t)strlen(s), 200);
}

void Diag_TestUART(void)
{
    /* 发送多字节测试字符，确认 USART3 TX (PD8) 硬件链路工作。
     * 如果串口助手能收到这段文字，说明硬件 + 配置 + 波特率都正确。
     * 如果收不到，问题在硬件接线或 PD8 没接到 USB-TTL RX。 */
    diag_send("\r\n\r\n");
    diag_send("================================\r\n");
    diag_send("=== STM32 USART3 DIAG TEST ===\r\n");
    diag_send("================================\r\n");
    diag_send("If you see this text, USART3 TX (PD8) is working!\r\n");
    diag_send("Config: 115200 baud, 8N1, PD8=TX, PD9=RX\r\n");
    diag_send("If you see nothing but USB-TTL is connected,\r\n");
    diag_send("check: 1) PD8 -> USB-TTL RX  2) GND common  3) baudrate 115200\r\n");
    diag_send("================================\r\n\r\n");
}

void Diag_ReportKey(const key_state_t *key)
{
    /* 仅在按键状态变化时报告，避免每帧刷屏 */
    static key_state_t prev;
    static int first = 1;

    if (first)
    {
        prev = *key;
        first = 0;
        return;
    }

    if (memcmp(key, &prev, sizeof(key_state_t)) != 0)
    {
        char buf[128];
        int len = snprintf(buf, sizeof(buf),
            "[KEY] u=%d d=%d l=%d r=%d ok=%d back=%d ec=%d id=%d\r\n",
            key->up, key->down, key->left, key->right,
            key->ok, key->back, key->ec_sw, key->id_connected);
        if (len > 0 && len < (int)sizeof(buf))
            HAL_UART_Transmit(&huart3, (uint8_t*)buf, (uint16_t)len, 100);
        prev = *key;
    }
}

void Diag_Tick(void)
{
    static uint32_t last_tick = 0;
    uint32_t now = HAL_GetTick();

    if (now - last_tick >= 1000)
    {
        char buf[64];
        int len = snprintf(buf, sizeof(buf), "[DIAG] alive tick=%u\r\n", now);
        if (len > 0 && len < (int)sizeof(buf))
            HAL_UART_Transmit(&huart3, (uint8_t*)buf, (uint16_t)len, 100);
        last_tick = now;
    }
}
