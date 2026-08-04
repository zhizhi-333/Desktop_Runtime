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
#include "gui.h"
#include "lcd.h"
#include "rtc_time.h"
#include "file_sys.h"
#include "file_task.h"
#include "music_task.h"
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

/* ---- 系统错误标志（位图） ---- */
static uint32_t sys_err_flags = SYS_ERR_NONE;

/* ---- 看门狗 (IWDG) ---- */
static IWDG_HandleTypeDef hiwdg;

/* ---- 任务心跳计数器(volatile: 中断/多任务读取) ---- */
volatile uint32_t task_heartbeats[HB_TASK_COUNT] = {0};

/* ---- 运行负载分析历史峰值(由 MonitorTask 更新, Monitor_GetData 读取) ---- */
uint32_t load_min_stacks[5];    /* 各任务栈历史最小值(最大使用) */
uint32_t load_peak_queues[3];   /* 队列历史峰值: [0]=FileReq [1]=FileResp [2]=MusicCmd */

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

/* ---- eTaskState 转字符（R=Running r=Ready B=Blocked S=Suspended D=Deleted） ---- */
static char task_state_char(eTaskState s)
{
    switch (s)
    {
        case eRunning:   return 'R';
        case eReady:     return 'r';
        case eBlocked:   return 'B';
        case eSuspended: return 'S';
        case eDeleted:   return 'D';
        default:         return '?';
    }
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
    data->file_stack    = FileTask_GetStackWatermark();
    data->music_stack   = MusicTask_GetStackWatermark();
    data->file_req_qwm  = (uint32_t)FileTask_GetReqWatermark();
    data->file_resp_qwm = (uint32_t)FileTask_GetRespWatermark();
    data->music_cmd_qwm = (uint32_t)MusicTask_GetCmdWatermark();
    data->task_states[0] = task_state_char(eTaskGetState(monitor_task_handle));
    data->task_states[1] = task_state_char(eTaskGetState(led_task_handle));
    data->task_states[2] = task_state_char(eTaskGetState(input_task_handle));
    data->task_states[3] = task_state_char(FileTask_GetState());
    data->task_states[4] = task_state_char(MusicTask_GetState());
    data->task_states[5] = 0;

    /* ---- 历史峰值(运行负载分析) ---- */
    data->min_monitor_stack = load_min_stacks[0];
    data->min_led_stack     = load_min_stacks[1];
    data->min_input_stack   = load_min_stacks[2];
    data->min_file_stack    = load_min_stacks[3];
    data->min_music_stack   = load_min_stacks[4];
    data->peak_file_req_qwm  = load_peak_queues[0];
    data->peak_file_resp_qwm = load_peak_queues[1];
    data->peak_music_cmd_qwm = load_peak_queues[2];
}

/* ============================================================
 * 看门狗 (IWDG) + 任务心跳
 * ============================================================ */

/* 初始化 IWDG 硬件看门狗
 * LSI ≈ 32kHz, Prescaler=256 -> 125Hz, Reload=1562 -> ~12.5s 超时
 * (留足够时间让 MonitorTask 每秒喂狗,并容忍短暂阻塞) */
void Monitor_WDG_Init(void)
{
    hiwdg.Instance       = IWDG;
    hiwdg.Init.Prescaler = IWDG_PRESCALER_256;
    hiwdg.Init.Reload    = 1562;
    /* 本 HAL 版本无 Window 字段, 不设置即默认禁用窗口模式 */
    if (HAL_IWDG_Init(&hiwdg) != HAL_OK)
    {
        Log_Printf("[WDT] !!! IWDG init FAILED !!!\r\n");
        return;
    }
    Log_Printf("[WDT] IWDG initialized (timeout ~12.5s)\r\n");
}

/* 任务心跳上报 */
void Monitor_Heartbeat(hb_task_id_t id)
{
    if (id < HB_TASK_COUNT)
        task_heartbeats[id]++;
}

void Monitor_IncInputEvent(void)    { evt_input++; }
void Monitor_IncDroppedEvent(void)  { evt_dropped++; }
void Monitor_IncError(void)         { evt_error++; }
void Monitor_SetState(sys_runtime_state_t s) { cur_state = s; }
int  Monitor_IsScreenOff(void)      { return 0; }  /* 兼容接口，实际状态由 InputTask 管理 */

/* ---- 系统错误标志接口 ---- */
void Monitor_SetError(uint32_t err_mask)
{
    sys_err_flags |= err_mask;
}

void Monitor_ClearError(uint32_t err_mask)
{
    sys_err_flags &= ~err_mask;
}

uint32_t Monitor_GetError(void)
{
    return sys_err_flags;
}

const char *Monitor_GetErrorString(void)
{
    static char buf[32];
    if (sys_err_flags == SYS_ERR_NONE)
        return "OK";

    /* 拼接错误标识，优先级：INPUT > RTC > SD > STACK > HEAP */
    int pos = 0;
    if (sys_err_flags & SYS_ERR_INPUT) { buf[pos++]='I'; buf[pos++]='N'; buf[pos++]='P'; }
    if (sys_err_flags & SYS_ERR_RTC)   { if(pos) buf[pos++]=','; buf[pos++]='R'; buf[pos++]='T'; buf[pos++]='C'; }
    if (sys_err_flags & SYS_ERR_SD)    { if(pos) buf[pos++]=','; buf[pos++]='S'; buf[pos++]='D'; }
    if (sys_err_flags & SYS_ERR_STACK) { if(pos) buf[pos++]=','; buf[pos++]='S'; buf[pos++]='T'; buf[pos++]='K'; }
    if (sys_err_flags & SYS_ERR_HEAP)  { if(pos) buf[pos++]=','; buf[pos++]='H'; buf[pos++]='P'; }
    if (sys_err_flags & SYS_ERR_TASK_HANG) { if(pos) buf[pos++]=','; buf[pos++]='H'; buf[pos++]='A'; buf[pos++]='N'; buf[pos++]='G'; }
    buf[pos] = 0;
    return buf;
}

/* ---- 启动界面：显示 LOGO 和加载信息，停留 2 秒 ---- */
static void show_boot_screen(void)
{
    LCD_Clear(BLACK);

    /* 顶部标题：项目名 */
    GUI_DrawString(60, 70, "STM32F407 HMI", CYAN, BLACK, 3);
    GUI_DrawString(120, 120, "Smart Human-Machine Interface", YELLOW, BLACK, 1);

    /* 中部分隔线 */
    LCD_Fill(60, 150, 420, 151, CYAN);

    /* 加载信息行 */
    GUI_DrawString(80, 170, "[OK] System initialized", GREEN, BLACK, 1);
    GUI_DrawString(80, 190, "[OK] FreeRTOS started", GREEN, BLACK, 1);
    GUI_DrawString(80, 210, "[OK] LCD driver loaded", GREEN, BLACK, 1);
    GUI_DrawString(80, 230, "[..] Loading login...", YELLOW, BLACK, 1);

    /* 底部进度条边框 */
    GUI_DrawRect(60, 270, 360, 16, WHITE);
    /* 进度条填充（简单一次填满，配合 2 秒停留） */
    LCD_Fill(61, 271, 419, 285, GREEN);

    /* 底部版本信息 */
    GUI_DrawString(150, 295, "v1.0  2026", GRAY, BLACK, 1);

    /* 停留 2 秒 */
    vTaskDelay(pdMS_TO_TICKS(2000));

    /* 清屏进入登录界面 */
    LCD_Clear(BLACK);
}

/* ---- 输入任务：扫描矩阵键盘+编码器，按需刷新屏幕 UI ---- */
static void InputTask(void *arg)
{
    (void)arg;
    sys_state_t state = SYS_LOGIN;
    key_state_t key, prev_key;
    memset(&prev_key, 0, sizeof(prev_key));
    memset(&empty_key, 0, sizeof(empty_key));

    /* 启动界面（停留 2 秒后进入登录） */
    show_boot_screen();

    /* ---- 初始化 RTC（移到此处避免调度器前 LSE 超时阻塞导致白屏） ---- */
    {
        LCD_Clear(BLACK);
        GUI_DrawString(140, 140, "Loading RTC...", YELLOW, BLACK, 2);
        Log_Printf("[INPUT] RTC init...\r\n");
        RTC_Init();
        Log_Printf("[INPUT] RTC init done: %02d:%02d:%02d\r\n",
                   RTC_GetHour(), RTC_GetMinute(), RTC_GetSecond());

        /* RTC 错误检测：首次上电（INITS=0）说明 RTC 未正常保持 */
        {
            extern int rtc_first_power_on;  /* rtc_time.c 中定义 */
            if (rtc_first_power_on)
            {
                Monitor_SetError(SYS_ERR_RTC);
                Log_Printf("[INPUT] RTC WARNING: first power-on, time not preserved\r\n");
            }
        }
    }

    /* ---- 初始化 SD 卡 + 文件系统(通过 FileTask 异步执行,不阻塞输入) ---- */
    {
        file_req_t req;
        file_resp_t resp;
        LCD_Clear(BLACK);
        GUI_DrawString(120, 140, "Loading SD card...", YELLOW, BLACK, 2);
        Log_Printf("[INPUT] SD card init (via FileTask)...\r\n");

        memset(&req, 0, sizeof(req));
        req.op = FILE_OP_INIT;
        FileTask_Request(&req, pdMS_TO_TICKS(1000));

        /* 阻塞等待 INIT 响应(启动阶段,用户无输入,可接受) */
        if (FileTask_GetResponse(&resp, pdMS_TO_TICKS(5000)))
        {
            if (resp.result != 0)
            {
                Monitor_SetError(SYS_ERR_SD);
                Log_Printf("[INPUT] SD card init FAILED (SD_ERR flag set)\r\n");
            }
            else
            {
                Log_Printf("[INPUT] SD card init OK, files=%d\r\n", FileSys_GetCount());
            }
        }
        else
        {
            Monitor_SetError(SYS_ERR_SD);
            Log_Printf("[INPUT] SD card init TIMEOUT\r\n");
        }
    }

    /* 清屏进入登录 */
    LCD_Clear(BLACK);

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
            /* 更新错误标志：设备断开时设置，恢复时清除 */
            if (key.id_connected)
                Monitor_ClearError(SYS_ERR_INPUT);
            else
                Monitor_SetError(SYS_ERR_INPUT);
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
        Monitor_Heartbeat(HB_INPUT);
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
        Monitor_Heartbeat(HB_LED);
        vTaskDelay(pdMS_TO_TICKS(500));
    }
}

/* ---- 监控任务：每秒打印系统健康状态 ---- */
static void MonitorTask(void *arg)
{
    (void)arg;
    TickType_t last = xTaskGetTickCount();
    uint32_t prev_hb[HB_TASK_COUNT];      /* 上一轮心跳值(检测是否增长) */
    uint32_t hang_seconds[HB_TASK_COUNT]; /* 各任务连续未心跳的秒数 */
    int i;

    /* 历史峰值(运行负载分析) - 记录最大栈使用=最小剩余 */
    static uint32_t hist_min_mon_stack = (uint32_t)-1;
    static uint32_t hist_min_led_stack = (uint32_t)-1;
    static uint32_t hist_min_inp_stack = (uint32_t)-1;
    static uint32_t hist_min_fil_stack = (uint32_t)-1;
    static uint32_t hist_min_mus_stack = (uint32_t)-1;
    static uint32_t hist_peak_file_req = 0;
    static uint32_t hist_peak_file_resp = 0;
    static uint32_t hist_peak_music_cmd = 0;

    /* 将历史峰值导出给 Monitor_GetData */
    #define UPDATE_MIN(dst, val) do { if ((val) < (dst)) (dst) = (val); } while(0)
    #define UPDATE_MAX(dst, val) do { if ((val) > (dst)) (dst) = (val); } while(0)

    Log_Printf("[MON] task entered, tick=%u\r\n", (unsigned)xTaskGetTickCount());
    Log_Printf("\r\n========== FreeRTOS Started ==========\r\n");
    Log_Printf("CPU: STM32F407ZG @ 168MHz\r\n");
    Log_Printf("Tick: 1000Hz, Heap: %u bytes, MaxPrior: %d\r\n",
               (unsigned)configTOTAL_HEAP_SIZE, configMAX_PRIORITIES);
    Log_Printf("======================================\r\n\r\n");

    /* 心跳检测初始化 */
    for (i = 0; i < HB_TASK_COUNT; i++)
    {
        prev_hb[i] = task_heartbeats[i];
        hang_seconds[i] = 0;
    }

    for (;;)
    {
        size_t free_heap = xPortGetFreeHeapSize();
        if (free_heap < min_ever_heap) min_ever_heap = free_heap;

        /* ---- 任务心跳检测 ---- */
        int all_alive = 1;
        for (i = 0; i < HB_TASK_COUNT; i++)
        {
            if (task_heartbeats[i] != prev_hb[i])
            {
                prev_hb[i] = task_heartbeats[i];
                hang_seconds[i] = 0;
            }
            else
            {
                hang_seconds[i]++;
                /* 超过 5 秒(5 次循环)无心跳 -> 卡死 */
                if (hang_seconds[i] >= 5)
                {
                    all_alive = 0;
                    if (!(sys_err_flags & SYS_ERR_TASK_HANG))
                    {
                        Monitor_SetError(SYS_ERR_TASK_HANG);
                        Log_Printf("[WDT] !!! task %d hang detected, stopping watchdog feed !!!\r\n", i);
                    }
                }
            }
        }

        /* ---- 喂狗: 仅当所有任务健康时 ---- */
        if (all_alive)
        {
            HAL_IWDG_Refresh(&hiwdg);
            /* 卡死恢复后清除标志 */
            if (sys_err_flags & SYS_ERR_TASK_HANG)
            {
                Monitor_ClearError(SYS_ERR_TASK_HANG);
                Log_Printf("[WDT] tasks recovered, watchdog feed resumed\r\n");
            }
        }
        /* else: 不喂狗, IWDG 超时后复位整个系统 */

        /* ---- 更新历史峰值(运行负载分析) ---- */
        {
            uint32_t ms = (uint32_t)uxTaskGetStackHighWaterMark(monitor_task_handle);
            uint32_t ls = (uint32_t)uxTaskGetStackHighWaterMark(led_task_handle);
            uint32_t is = (uint32_t)uxTaskGetStackHighWaterMark(input_task_handle);
            uint32_t fs = FileTask_GetStackWatermark();
            uint32_t us = MusicTask_GetStackWatermark();
            uint32_t fr = (uint32_t)FileTask_GetReqWatermark();
            uint32_t fp = (uint32_t)FileTask_GetRespWatermark();
            uint32_t mc = (uint32_t)MusicTask_GetCmdWatermark();
            UPDATE_MIN(hist_min_mon_stack, ms);
            UPDATE_MIN(hist_min_led_stack, ls);
            UPDATE_MIN(hist_min_inp_stack, is);
            UPDATE_MIN(hist_min_fil_stack, fs);
            UPDATE_MIN(hist_min_mus_stack, us);
            UPDATE_MAX(hist_peak_file_req, fr);
            UPDATE_MAX(hist_peak_file_resp, fp);
            UPDATE_MAX(hist_peak_music_cmd, mc);
            /* 保存到全局供 Monitor_GetData 读取 */
            load_min_stacks[0] = hist_min_mon_stack;
            load_min_stacks[1] = hist_min_led_stack;
            load_min_stacks[2] = hist_min_inp_stack;
            load_min_stacks[3] = hist_min_fil_stack;
            load_min_stacks[4] = hist_min_mus_stack;
            load_peak_queues[0] = hist_peak_file_req;
            load_peak_queues[1] = hist_peak_file_resp;
            load_peak_queues[2] = hist_peak_music_cmd;
        }

        Log_Printf("[MON] tick=%u  free_heap=%u  min_heap=%u  monitor_stack=%u  led_stack=%u\r\n",
                   (unsigned)xTaskGetTickCount(),
                   (unsigned)free_heap,
                   (unsigned)min_ever_heap,
                   (unsigned)uxTaskGetStackHighWaterMark(monitor_task_handle),
                   (unsigned)uxTaskGetStackHighWaterMark(led_task_handle));

        Monitor_Heartbeat(HB_MONITOR);  /* 自身心跳 */

        vTaskDelayUntil(&last, pdMS_TO_TICKS(1000));
    }
}

/* ---- 栈溢出钩子：configCHECK_FOR_STACK_OVERFLOW=2 时被调用 ---- */
/* 注意：钩子里绝对不能调用可能引起任务切换的 API（如 xSemaphoreCreateMutex）
 * 否则会再次触发任务切换 → 再次检测栈溢出 → 死循环 */
void vApplicationStackOverflowHook(TaskHandle_t xTask, char *pcTaskName)
{
    (void)xTask;
    sys_err_flags |= SYS_ERR_STACK;  /* 标记栈溢出错误（死循环前记录） */
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
    sys_err_flags |= SYS_ERR_HEAP;  /* 标记堆分配失败 */
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

    /* FileTask: 后台文件 I/O,优先级 2 (与 LED 同级,低于 Input) */
    FileTask_Start();
    /* MusicTask: 后台音乐播放,优先级 2 */
    MusicTask_Start();
}
