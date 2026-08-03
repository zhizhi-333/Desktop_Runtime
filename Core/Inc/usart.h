#ifndef USART_H
#define USART_H

#include "main.h"
#include "stm32f4xx_hal_uart.h"

extern UART_HandleTypeDef huart3;

void MX_USART3_UART_Init(void);

/* 日志打印接口（线程安全，可在任务中直接调用） */
void Log_Printf(const char *fmt, ...);

#endif /* USART_H */
