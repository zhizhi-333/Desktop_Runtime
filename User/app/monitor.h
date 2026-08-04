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
    char        task_states[6];     /* 5 个任务运行状态字符 + null */
    /* 顺序: [0]=Monitor [1]=LED [2]=Input [3]=File [4]=Music */
    /* 字符: R=Running r=Ready B=Blocked S=Suspended D=Deleted */
    /* ---- 运行负载分析: 历史峰值(MonitorTask 每秒更新) ---- */
    uint32_t    min_monitor_stack;  /* Monitor 栈历史最小值(最大使用) */
    uint32_t    min_led_stack;      /* LED 栈历史最小值 */
    uint32_t    min_input_stack;    /* Input 栈历史最小值 */
    uint32_t    min_file_stack;     /* FileTask 栈历史最小值 */
    uint32_t    min_music_stack;    /* MusicTask 栈历史最小值 */
    uint32_t    peak_file_req_qwm;  /* 文件请求队列历史峰值 */
    uint32_t    peak_file_resp_qwm; /* 文件响应队列历史峰值 */
    uint32_t    peak_music_cmd_qwm; /* 音乐命令队列历史峰值 */
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
#define SYS_ERR_TASK_HANG       0x20    /* 任务卡死（看门狗检测到,即将复位） */

/* 设置/清除错误标志 */
void Monitor_SetError(uint32_t err_mask);
void Monitor_ClearError(uint32_t err_mask);

/* 获取当前错误标志（位图） */
uint32_t Monitor_GetError(void);

/* 获取错误提示字符串（用于桌面状态栏显示） */
/* 返回: 指向静态字符串的指针，无错误时返回 "OK" */
const char *Monitor_GetErrorString(void);

/* ============================================================
 * 看门狗 (IWDG) + 任务心跳检测
 *
 * 原理:
 *   - IWDG 硬件看门狗: 超时未喂狗则整个系统复位
 *   - 软件任务心跳: 每个任务在循环中调用 Monitor_Heartbeat()
 *     MonitorTask 每秒检查 5 个心跳计数是否变化
 *     若某任务 5 秒内心跳无变化 -> 判定卡死, 停止喂狗 -> IWDG 复位
 *
 * 使用:
 *   - 启动: Monitor_WDG_Init() (在 main.c 调度器启动前)
 *   - 各任务循环中: Monitor_Heartbeat(task_id)
 *   - 喂狗由 MonitorTask 自动完成(检测到所有任务健康时)
 * ============================================================ */

/* 任务 ID (用于心跳上报) */
typedef enum {
    HB_MONITOR = 0,
    HB_LED,
    HB_INPUT,
    HB_FILE,
    HB_MUSIC,
    HB_TASK_COUNT,          /* 任务总数(=5) */
} hb_task_id_t;

/* 初始化 IWDG 硬件看门狗(在 main.c 调度器启动前调用一次) */
void Monitor_WDG_Init(void);

/* 任务心跳上报(各任务在主循环中调用) */
void Monitor_Heartbeat(hb_task_id_t id);

#endif /* MONITOR_H */
