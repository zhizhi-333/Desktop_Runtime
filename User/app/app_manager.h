#ifndef APP_MANAGER_H
#define APP_MANAGER_H

#include "key.h"

/* ============================================================
 * 应用管理器
 *
 * 单前台模式：同一时刻只有一个应用在前台运行
 * 切换应用时：当前应用 on_pause → 新应用 on_start
 * ============================================================ */

/* 应用状态 */
typedef enum {
    APP_STATE_NONE,     /* 未启动 */
    APP_STATE_PAUSED,   /* 已创建但被暂停（保留状态） */
    APP_STATE_RUNNING,  /* 前台运行中 */
} app_state_t;

/* 应用入口结构体 */
typedef struct {
    const char *name;                   /* 应用名称（显示在图标下） */
    void (*on_create)(void);            /* 首次进入：初始化资源 */
    void (*on_start)(void);             /* 从暂停恢复：继续运行 */
    void (*on_run)(key_state_t *key);   /* 运行中：每帧调用（20ms周期） */
    void (*on_pause)(void);             /* 被切换走：保存状态（可选） */
    void (*on_exit)(void);              /* 关闭退出：释放资源/暂停音频/写回状态 */
} app_entry_t;

/* 运行时界面状态（AppManager 内部使用，区分桌面/应用） */
typedef enum {
    RT_DESKTOP,     /* 桌面 */
    RT_APP,         /* 应用内 */
} rt_state_t;

/* 初始化应用管理器 */
void AppManager_Init(void);

/* 获取当前运行时状态 */
rt_state_t AppManager_GetState(void);

/* 获取当前前台应用索引（-1 表示无） */
int AppManager_GetCurrentApp(void);

/* 进入桌面（从应用返回） */
void AppManager_GotoDesktop(void);

/* 启动指定应用（从桌面进入）
 * idx: 应用在注册表中的索引
 * 返回：0=成功, -1=索引非法 */
int AppManager_StartApp(int idx);

/* 每帧调用：根据系统状态分发到桌面或应用 */
void AppManager_Run(key_state_t *key);

/* 获取应用总数 */
int AppManager_GetAppCount(void);

/* 获取应用入口（供桌面画图标用） */
const app_entry_t *AppManager_GetApp(int idx);

#endif /* APP_MANAGER_H */
