/*
 * FreeRTOS V202212.00
 * Copyright (C) 2020 Amazon.com, Inc. or its affiliates. All Rights Reserved.
 *
 * Permission is hereby granted, free of charge, to any person obtaining a copy of
 * this software and associated documentation files (the "Software"), to deal in
 * the Software without restriction, including without limitation the rights to
 * use, copy, modify, merge, publish, distribute, sublicense, and/or sell copies of
 * the Software, and to permit persons to whom the Software is furnished to do so,
 * subject to the following conditions:
 *
 * The above copyright notice and this permission notice shall be included in all
 * copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY, FITNESS
 * FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE AUTHORS OR
 * COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER
 * IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM, OUT OF OR IN
 * CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE SOFTWARE.
 *
 * https://www.FreeRTOS.org
 * https://github.com/FreeRTOS
 *
 */

#ifndef FREERTOS_CONFIG_H
#define FREERTOS_CONFIG_H

#include "stm32f4xx_hal.h"

/*-----------------------------------------------------------
 * Application specific definitions.
 *
 * These definitions should be adjusted for your particular hardware and
 * application requirements.
 *
 * THESE PARAMETERS ARE DESCRIBED WITHIN THE 'CONFIGURATION' SECTION OF THE
 * FreeRTOS API DOCUMENTATION AVAILABLE ON THE FreeRTOS.org WEB SITE. 
 *
 * See http://www.freertos.org/a00110.html
 *----------------------------------------------------------*/

#define configUSE_PREEMPTION		1
#define configUSE_IDLE_HOOK			0
#define configUSE_TICK_HOOK			0
#define configCPU_CLOCK_HZ			( ( unsigned long ) 168000000 )
#define configTICK_RATE_HZ			( ( TickType_t ) 1000 )
#define configMAX_PRIORITIES		( 7 )
#define configMINIMAL_STACK_SIZE	( ( unsigned short ) 128 )
#define configTOTAL_HEAP_SIZE		( ( size_t ) ( 48 * 1024 ) )
#define configMAX_TASK_NAME_LEN		( 16 )
#define configUSE_TRACE_FACILITY	1
#define configUSE_16_BIT_TICKS		0
#define configIDLE_SHOULD_YIELD		1

/* Co-routine definitions. */
#define configUSE_CO_ROUTINES 		0
#define configMAX_CO_ROUTINE_PRIORITIES ( 2 )

/* 内存保护：开启栈溢出检测（方式2：全面检查） */
#define configCHECK_FOR_STACK_OVERFLOW	2

/* 内存分配失败钩子 */
#define configUSE_MALLOC_FAILED_HOOK	1

/* 静态分配 API（xTaskCreateStatic 等） */
#define configSUPPORT_STATIC_ALLOCATION	0

/* 动态分配 API（xTaskCreate 等） */
#define configSUPPORT_DYNAMIC_ALLOCATION	1

/* 可选 API：互斥量、递归互斥量、计数信号量 */
#define configUSE_MUTEXES				1
#define configUSE_RECURSIVE_MUTEXES		1
#define configUSE_COUNTING_SEMAPHORES	1

/* 队列注册表（调试用，可追踪队列） */
#define configQUEUE_REGISTRY_SIZE		8

/* 软件定时器 */
#define configUSE_TIMERS				1
#define configTIMER_TASK_PRIORITY		3
#define configTIMER_QUEUE_LENGTH		10
#define configTIMER_TASK_STACK_DEPTH	512

/* 运行时统计：用于任务 CPU 占比分析 */
#define configGENERATE_RUN_TIME_STATS	1
#define portCONFIGURE_TIMER_FOR_RUN_TIME_STATS()  /* 暂用空宏，后续可挂 TIM2 */
#define portGET_RUN_TIME_COUNTER_VALUE()          (0)

/* Set the following definitions to 1 to include the API function, or zero
to exclude the API function. */

#define INCLUDE_vTaskPrioritySet		1
#define INCLUDE_uxTaskPriorityGet		1
#define INCLUDE_vTaskDelete				1
#define INCLUDE_vTaskCleanUpResources	0
#define INCLUDE_vTaskSuspend			1
#define INCLUDE_vTaskDelayUntil			1
#define INCLUDE_vTaskDelay				1
#define INCLUDE_uxTaskGetStackHighWaterMark  1
#define INCLUDE_xTaskGetSchedulerState 1
#define INCLUDE_xTaskGetHandle          1
#define INCLUDE_eTaskGetState           1

/* 调试辅助：允许 vTaskList / vTaskGetRunTimeStats 格式化输出 */
#define configUSE_STATS_FORMATTING_FUNCTIONS  1

/* STM32F4 使用 4 位优先级（高 4 位有效，共 16 个优先级 0-15） */
#define configPRIO_BITS							4

/* 最低优先级（PendSV/SysTick 用），raw 值 0-15 */
#define configLIBRARY_LOWEST_INTERRUPT_PRIORITY		15
/* 兼容旧名称（main.c 中 HAL_NVIC_SetPriority 使用） */
#define configLIBRARY_KERNEL_INTERRUPT_PRIORITY		configLIBRARY_LOWEST_INTERRUPT_PRIORITY
/* 可调用 FreeRTOS ISR API 的最高优先级（raw 值 0-15），优先级 5 */
#define configLIBRARY_MAX_SYSCALL_INTERRUPT_PRIORITY	5

/* 移位后的实际寄存器值（高 4 位） */
#define configKERNEL_INTERRUPT_PRIORITY 		( configLIBRARY_LOWEST_INTERRUPT_PRIORITY << (8 - configPRIO_BITS) )
#define configMAX_SYSCALL_INTERRUPT_PRIORITY 	( configLIBRARY_MAX_SYSCALL_INTERRUPT_PRIORITY << (8 - configPRIO_BITS) )

/* 中断映射：用 FreeRTOS 官方移植的 PendSV/SVC 符号 */
#define xPortPendSVHandler PendSV_Handler
#define vPortSVCHandler SVC_Handler

/* 断言：触发时关中断 + 串口打印 + 死循环，便于定位问题 */
extern UART_HandleTypeDef huart3;
#define configASSERT( x ) \
    do { \
        if( ( x ) == 0 ) \
        { \
            taskDISABLE_INTERRUPTS(); \
            HAL_UART_Transmit(&huart3, (uint8_t*)"\r\n!!! ASSERT FAIL !!!\r\n", 24, 100); \
            for(;;); \
        } \
    } while(0)

#endif /* FREERTOS_CONFIG_H */
