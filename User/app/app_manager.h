#ifndef APP_MANAGER_H
#define APP_MANAGER_H

#include "key.h"

/* ============================================================
 * 应用管理器
 *
 * 单前台模式 + 任务栏最小化：
 *   - 同一时刻只有一个应用在前台运行
 *   - 编码器按压 = 最小化当前应用（挂起到任务栏，音乐继续播放）
 *   - BACK = 退出当前应用（彻底关闭，回桌面）
 *   - 桌面任务栏显示已最小化的应用，点击切换回来
 *
 * 切换应用时：当前应用 on_pause → 新应用 on_start
 * ============================================================ */

/* 最大最小化应用数（任务栏最多显示数） */
#define MAX_MINIMIZED  4

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

/* 进入桌面（从应用返回，彻底关闭当前应用）
 * 等价于"关闭"：当前应用从最小化列表移除，状态置 NONE */
void AppManager_GotoDesktop(void);

/* 最小化当前应用（挂起到任务栏，调用 on_pause 保存状态）
 * 音乐等独立任务应用最小化后继续后台运行
 * 返回：0=成功, -1=无前台应用或任务栏已满 */
int AppManager_MinimizeCurrent(void);

/* 从任务栏恢复指定最小化应用到前台
 * slot: 最小化列表中的位置(0~MAX_MINIMIZED-1)
 * 返回：0=成功, -1=非法 slot */
int AppManager_RestoreMinimized(int slot);

/* 获取最小化应用数量 */
int AppManager_GetMinimizedCount(void);

/* 获取最小化列表中第 slot 个应用的注册表索引
 * 返回：>=0 应用索引, -1=该 slot 为空 */
int AppManager_GetMinimizedApp(int slot);

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
