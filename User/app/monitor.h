#ifndef MONITOR_H
#define MONITOR_H

#include "main.h"
#include <stdint.h>

/* 创建并启动监控任务和 LED 任务 */
void Monitor_Start(void);

/* ============================================================
 * 系统监控数据接口（供系统监控应用读取）
 * ============================================================ */

/* 系统状态字符串 */
typedef enum {
    SYS_STATE_BOOT,     /* 启动中 */
    SYS_STATE_LOGIN,    /* 登录界面 */
    SYS_STATE_DESKTOP,  /* 桌面 */
    SYS_STATE_APP,      /* 应用内 */
} sys_runtime_state_t;

/* 监控数据快照（一次读取所有数据，保证一致性） */
typedef struct {
    uint32_t    tick;               /* 当前 tick（ms） */
    uint32_t    uptime_sec;         /* 运行时间（秒） */
    sys_runtime_state_t state;      /* 当前系统状态 */
    size_t      free_heap;          /* 剩余堆（字节） */
    size_t      min_heap;           /* 历史最小剩余堆 */
    uint32_t    input_events;       /* 输入事件计数 */
    uint32_t    dropped_events;     /* 丢弃事件计数 */
    uint32_t    error_count;        /* 错误计数 */
    uint32_t    monitor_stack;      /* Monitor 栈剩余（字） */
    uint32_t    led_stack;          /* LED 栈剩余（字） */
    uint32_t    input_stack;        /* Input 栈剩余（字） */
    uint32_t    file_stack;         /* FileTask 栈剩余（字） */
    uint32_t    music_stack;        /* MusicTask 栈剩余（字） */
    uint32_t    file_req_qwm;       /* 文件请求队列水位 */
    uint32_t    file_resp_qwm;      /* 文件响应队列水位 */
    uint32_t    music_cmd_qwm;      /* 音乐命令队列水位 */
} monitor_data_t;

/* 获取监控数据快照 */
void Monitor_GetData(monitor_data_t *data);

/* ---- 事件计数接口（供其他模块累加） ---- */

/* 输入事件（每次有效按键） */
void Monitor_IncInputEvent(void);

/* 丢弃事件（去抖失败/重复按键等） */
void Monitor_IncDroppedEvent(void);

/* 错误事件（应用错误/异常等） */
void Monitor_IncError(void);

/* 设置当前系统状态 */
void Monitor_SetState(sys_runtime_state_t state);

/* 查询屏幕是否熄灭（1=熄屏, 0=亮屏） */
int Monitor_IsScreenOff(void);

/* ============================================================
 * 系统错误标志接口（供桌面状态栏显示错误提示）
 * ============================================================ */

/* 系统错误码（位图，可同时存在多个错误） */
#define SYS_ERR_NONE            0x00
#define SYS_ERR_INPUT           0x01    /* 输入设备断开 */
#define SYS_ERR_RTC             0x02    /* RTC 异常（时间未保持/初始化失败） */
#define SYS_ERR_SD              0x04    /* SD 卡异常 */
#define SYS_ERR_STACK           0x08    /* 栈溢出（曾经发生） */
#define SYS_ERR_HEAP            0x10    /* 堆分配失败（曾经发生） */

/* 设置/清除错误标志 */
void Monitor_SetError(uint32_t err_mask);
void Monitor_ClearError(uint32_t err_mask);

/* 获取当前错误标志（位图） */
uint32_t Monitor_GetError(void);

/* 获取错误提示字符串（用于桌面状态栏显示） */
/* 返回: 指向静态字符串的指针，无错误时返回 "OK" */
const char *Monitor_GetErrorString(void);

#endif /* MONITOR_H */
