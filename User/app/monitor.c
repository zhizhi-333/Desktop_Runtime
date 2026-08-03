#include "monitor.h"
#include "usart.h"
#include "stm32f4xx_hal.h"
#include "main.h"
#include "tim.h"
#include "FreeRTOS.h"
#include "task.h"
#include "input_test.h"
#include "login.h"
#include "app_manager.h"
#include "desktop.h"
#include "key.h"
#include "settings.h"
#include "log_store.h"
#include <stdio.h>
#include <string.h>

static TaskHandle_t monitor_task_handle;
static TaskHandle_t led_task_handle;
static TaskHandle_t input_task_handle;

/* 记录启动以来的最小剩余堆（只减不增，用于发现内存泄漏） */
static size_t min_ever_heap = (size_t)-1;

/* ---- 事件计数器（供监控应用显示） ---- */
static uint32_t evt_input    = 0;   /* 输入事件计数 */
static uint32_t evt_dropped  = 0;   /* 丢弃事件计数 */
static uint32_t evt_error    = 0;   /* 错误计数 */
static sys_runtime_state_t cur_state = SYS_STATE_BOOT;

/* 系统状态（InputTask 顶层状态机） */
typedef enum {
    SYS_LOGIN,      /* 首次登录界面 */
    SYS_RUNTIME,    /* 运行时（桌面+应用） */
    SYS_SCREEN_OFF, /* 熄屏（背光关，应用继续运行传空key） */
    SYS_LOCKED,     /* 锁屏（显示密码界面，解锁回原界面） */
} sys_state_t;

/* ---- 熄屏/锁屏状态 ---- */
static int need_lock_on_wake = 0;       /* 唤醒后是否需要锁屏密码 */
static int waking_up = 0;               /* 唤醒后等待按键释放 */
static TickType_t last_input_tick = 0;  /* 最后一次输入活动的时间戳 */
static key_state_t empty_key;           /* 空按键(熄屏时传给应用保持运行) */

/* ---- 编码器旋转检测 ---- */
/* 直接读 TIM4 计数器判断是否有旋转，不调用 Encoder_GetDelta
 * 以免消费掉应用需要的 delta。两个模块独立读取硬件寄存器互不影响 */
static uint16_t last_enc_cnt = 0;
static int enc_active = 0;              /* 本帧编码器是否有旋转 */

/* 检测是否有按键活动（用于 waking_up 等待释放，不含编码器旋转） */
static int has_key_release_activity(const key_state_t *key)
{
    return (key->up || key->down || key->left || key->right ||
            key->ok || key->back || key->ec_sw);
}

/* 检测是否有任意输入活动（含编码器旋转，用于唤醒和空闲计时） */
static int has_any_activity(const key_state_t *key)
{
    return has_key_release_activity(key) || enc_active;
}

/* 关背光 */
static void backlight_off(void)
{
    HAL_GPIO_WritePin(LCD_BL_GPIO_Port, LCD_BL_Pin, GPIO_PIN_RESET);
    LogStore_ScreenEvent(0);
    Log_Printf("[SCREEN] backlight off\r\n");
}

/* 开背光 */
static void backlight_on(void)
{
    HAL_GPIO_WritePin(LCD_BL_GPIO_Port, LCD_BL_Pin, GPIO_PIN_SET);
    LogStore_ScreenEvent(1);
    Log_Printf("[SCREEN] backlight on\r\n");
}

/* ---- 监控数据接口实现 ---- */
void Monitor_GetData(monitor_data_t *data)
{
    data->tick          = xTaskGetTickCount();
    data->uptime_sec    = data->tick / 1000;
    data->state         = cur_state;
    data->free_heap     = xPortGetFreeHeapSize();
    data->min_heap      = min_ever_heap;
    data->input_events  = evt_input;
    data->dropped_events= evt_dropped;
    data->error_count   = evt_error;
    data->monitor_stack = (uint32_t)uxTaskGetStackHighWaterMark(monitor_task_handle);
    data->led_stack     = (uint32_t)uxTaskGetStackHighWaterMark(led_task_handle);
    data->input_stack   = (uint32_t)uxTaskGetStackHighWaterMark(input_task_handle);
}

void Monitor_IncInputEvent(void)    { evt_input++; }
void Monitor_IncDroppedEvent(void)  { evt_dropped++; }
void Monitor_IncError(void)         { evt_error++; }
void Monitor_SetState(sys_runtime_state_t s) { cur_state = s; }
int  Monitor_IsScreenOff(void)      { return 0; }  /* 兼容接口，实际状态由 InputTask 管理 */

/* ---- 输入任务：扫描矩阵键盘+编码器，按需刷新屏幕 UI ---- */
static void InputTask(void *arg)
{
    (void)arg;
    sys_state_t state = SYS_LOGIN;
    key_state_t key, prev_key;
    memset(&prev_key, 0, sizeof(prev_key));
    memset(&empty_key, 0, sizeof(empty_key));

    Monitor_SetState(SYS_STATE_LOGIN);
    Login_Init();
    Log_Printf("[INPUT] task entered, state=LOGIN\r\n");
    last_input_tick = xTaskGetTickCount();
    last_enc_cnt = __HAL_TIM_GET_COUNTER(&htim4);

    for (;;)
    {
        Key_Scan(&key);

        /* ---- 编码器旋转检测 ---- */
        {
            uint16_t cur_cnt = __HAL_TIM_GET_COUNTER(&htim4);
            int16_t delta = (int16_t)(cur_cnt - last_enc_cnt);
            last_enc_cnt = cur_cnt;
            enc_active = (delta < -2 || delta > 2) ? 1 : 0;
        }

        /* ---- 唤醒等待：等按键全部释放，期间不处理任何状态 ---- */
        /* 防止按住唤醒键时反复触发唤醒。注意：这里只等按键释放，
         * 不含编码器旋转（旋转没有"释放"概念） */
        if (waking_up)
        {
            if (!has_key_release_activity(&key))
                waking_up = 0;
            /* 唤醒期间继续推进应用后台(音乐等) */
            if (state == SYS_RUNTIME || state == SYS_SCREEN_OFF)
                AppManager_Run(&empty_key);
            prev_key = key;
            vTaskDelay(pdMS_TO_TICKS(20));
            continue;
        }

        /* ---- SYS_SCREEN_OFF: 熄屏状态 ---- */
        if (state == SYS_SCREEN_OFF)
        {
            /* 继续推进应用后台(音乐等)，但传空key避免误触 */
            AppManager_Run(&empty_key);

            if (has_any_activity(&key))
            {
                /* 唤醒：开背光，设置 waking_up 阻止后续帧重复触发 */
                backlight_on();
                waking_up = 1;
                /* 关键：更新空闲计时，否则唤醒后立即又超时熄屏 */
                last_input_tick = xTaskGetTickCount();
                if (need_lock_on_wake)
                {
                    state = SYS_LOCKED;
                    Monitor_SetState(SYS_STATE_LOGIN);
                    Login_LockInit();
                    Log_Printf("[INPUT] wakeup -> locked\r\n");
                }
                else
                {
                    state = SYS_RUNTIME;
                    Log_Printf("[INPUT] wakeup -> runtime\r\n");
                }
            }
            prev_key = key;
            vTaskDelay(pdMS_TO_TICKS(20));
            continue;
        }

        /* ---- 更新空闲计时 ---- */
        if (has_any_activity(&key))
            last_input_tick = xTaskGetTickCount();

        /* ---- 超时熄屏(不锁屏) ---- */
        if (state == SYS_RUNTIME)
        {
            uint32_t timeout_ms = Settings_ScreenTimeout() * 1000U;
            if ((xTaskGetTickCount() - last_input_tick) > pdMS_TO_TICKS(timeout_ms))
            {
                state = SYS_SCREEN_OFF;
                need_lock_on_wake = 0;   /* 超时熄屏不锁屏 */
                backlight_off();
                prev_key = key;
                vTaskDelay(pdMS_TO_TICKS(20));
                continue;
            }
        }

        /* ---- 桌面下主动熄屏/锁屏快捷键 ---- */
        if (state == SYS_RUNTIME && AppManager_GetState() == RT_DESKTOP)
        {
            /* BACK: 主动熄屏(唤醒无密码) */
            if (key.back && !prev_key.back)
            {
                state = SYS_SCREEN_OFF;
                need_lock_on_wake = 0;
                backlight_off();
                last_input_tick = xTaskGetTickCount();
                waking_up = 1;   /* 等 BACK 键释放后才允许唤醒，否则按住时立即唤醒 */
                prev_key = key;
                vTaskDelay(pdMS_TO_TICKS(20));
                continue;
            }
            /* 编码器按压: 主动锁屏(唤醒需密码) */
            if (key.ec_sw && !prev_key.ec_sw)
            {
                state = SYS_SCREEN_OFF;
                need_lock_on_wake = 1;
                backlight_off();
                last_input_tick = xTaskGetTickCount();
                waking_up = 1;   /* 等 EC_SW 释放后才允许唤醒 */
                Log_Printf("[INPUT] manual lock\r\n");
                prev_key = key;
                vTaskDelay(pdMS_TO_TICKS(20));
                continue;
            }
        }

        /* ---- 设备连接状态变化日志 ---- */
        if (key.id_connected != prev_key.id_connected)
        {
            Log_Printf("[INPUT] device %s (id_connected=%d)\r\n",
                       key.id_connected ? "connected" : "disconnected",
                       key.id_connected);
        }

        /* ---- 正常处理 ---- */
        if (state == SYS_LOGIN)
        {
            if (Login_Run(&key))
            {
                state = SYS_RUNTIME;
                Monitor_SetState(SYS_STATE_DESKTOP);
                AppManager_Init();
                Log_Printf("[INPUT] login OK, entering runtime\r\n");
            }
        }
        else if (state == SYS_LOCKED)
        {
            /* 锁屏：密码正确后回到运行时原界面 */
            if (Login_Run(&key))
            {
                state = SYS_RUNTIME;
                if (AppManager_GetState() == RT_DESKTOP)
                {
                    Monitor_SetState(SYS_STATE_DESKTOP);
                    Desktop_Resume();
                }
                else
                {
                    Monitor_SetState(SYS_STATE_APP);
                    /* 调用当前应用的 on_start 恢复显示 */
                    {
                        int cur = AppManager_GetCurrentApp();
                        if (cur >= 0)
                        {
                            const app_entry_t *app = AppManager_GetApp(cur);
                            if (app && app->on_start) app->on_start();
                        }
                    }
                }
                Log_Printf("[INPUT] unlock OK, resuming\r\n");
            }
        }
        else  /* SYS_RUNTIME */
        {
            AppManager_Run(&key);
        }

        prev_key = key;
        vTaskDelay(pdMS_TO_TICKS(20));
    }
}

/* ---- LED 任务：500ms 翻转一次，肉眼确认调度器在跑 ---- */
static void LedTask(void *arg)
{
    (void)arg;
    Log_Printf("[LED] task entered\r\n");
    for (;;)
    {
        HAL_GPIO_TogglePin(LED0_GPIO_Port, LED0_Pin);
        vTaskDelay(pdMS_TO_TICKS(500));
    }
}

/* ---- 监控任务：每秒打印系统健康状态 ---- */
static void MonitorTask(void *arg)
{
    (void)arg;
    TickType_t last = xTaskGetTickCount();

    Log_Printf("[MON] task entered, tick=%u\r\n", (unsigned)xTaskGetTickCount());
    Log_Printf("\r\n========== FreeRTOS Started ==========\r\n");
    Log_Printf("CPU: STM32F407ZG @ 168MHz\r\n");
    Log_Printf("Tick: 1000Hz, Heap: %u bytes, MaxPrior: %d\r\n",
               (unsigned)configTOTAL_HEAP_SIZE, configMAX_PRIORITIES);
    Log_Printf("======================================\r\n\r\n");

    for (;;)
    {
        size_t free_heap = xPortGetFreeHeapSize();
        if (free_heap < min_ever_heap) min_ever_heap = free_heap;

        Log_Printf("[MON] tick=%u  free_heap=%u  min_heap=%u  monitor_stack=%u  led_stack=%u\r\n",
                   (unsigned)xTaskGetTickCount(),
                   (unsigned)free_heap,
                   (unsigned)min_ever_heap,
                   (unsigned)uxTaskGetStackHighWaterMark(monitor_task_handle),
                   (unsigned)uxTaskGetStackHighWaterMark(led_task_handle));

        vTaskDelayUntil(&last, pdMS_TO_TICKS(1000));
    }
}

/* ---- 栈溢出钩子：configCHECK_FOR_STACK_OVERFLOW=2 时被调用 ---- */
/* 注意：钩子里绝对不能调用可能引起任务切换的 API（如 xSemaphoreCreateMutex）
 * 否则会再次触发任务切换 → 再次检测栈溢出 → 死循环 */
void vApplicationStackOverflowHook(TaskHandle_t xTask, char *pcTaskName)
{
    (void)xTask;
    /* 用裸 UART 发送，不经过 Log_Printf（避免 mutex） */
    const char *prefix = "\r\n!!! STACK OVERFLOW in task: ";
    const char *suffix = " !!!\r\n";
    HAL_UART_Transmit(&huart3, (uint8_t*)prefix, strlen(prefix), 100);
    HAL_UART_Transmit(&huart3, (uint8_t*)pcTaskName, strlen(pcTaskName), 100);
    HAL_UART_Transmit(&huart3, (uint8_t*)suffix, strlen(suffix), 100);
    while (1);
}

/* ---- malloc 失败钩子 ---- */
void vApplicationMallocFailedHook(void)
{
    const char *msg = "\r\n!!! MALLOC FAILED !!!\r\n";
    HAL_UART_Transmit(&huart3, (uint8_t*)msg, strlen(msg), 100);
}

void Monitor_Start(void)
{
    /* MonitorTask 调用 vsnprintf 很吃栈，给 1024 字（4KB） */
    xTaskCreate(MonitorTask, "Monitor", 1024, NULL, 1, &monitor_task_handle);
    /* LedTask 很轻，256 字够用 */
    xTaskCreate(LedTask,     "LED",     256, NULL, 2, &led_task_handle);
    /* InputTask 负责键盘+编码器扫描和屏幕 UI，优先级最高保证响应实时性 */
    xTaskCreate(InputTask,   "Input",   512, NULL, 3, &input_task_handle);
}
