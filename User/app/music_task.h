#ifndef MUSIC_TASK_H
#define MUSIC_TASK_H

#include <stdint.h>
#include "FreeRTOS.h"
#include "task.h"
#include "queue.h"
#include "music.h"

/* ============================================================
 * 音乐播放后台任务
 *
 * 目的:
 *   - 将音符推进逻辑从 InputTask 中剥离
 *   - 熄屏/切换应用后音乐仍可独立播放
 *   - app_music 仅负责 UI 显示,通过队列发命令
 *
 * 工作模式:
 *   - app_music 通过 MusicTask_SendCmd() 发送命令
 *   - MusicTask 每 20ms 推进一次播放,调用 Music_Update()
 *   - 状态查询通过 Music_* 接口直接读(共享内存,需 dac mutex 保护)
 *
 * 命令队列水位供 SYSMONITOR 显示
 * ============================================================ */

/* ---- 命令类型 ---- */
typedef enum {
    MUSIC_CMD_NONE = 0,
    MUSIC_CMD_LOAD,     /* 加载曲目 (param1=曲目索引) */
    MUSIC_CMD_PLAY,     /* 开始播放 */
    MUSIC_CMD_PAUSE,    /* 暂停 */
    MUSIC_CMD_RESUME,   /* 恢复 */
    MUSIC_CMD_STOP,     /* 停止 */
    MUSIC_CMD_VOLUME,   /* 设置音量 (param1=0~100) */
} music_cmd_type_t;

/* ---- 命令结构体 ---- */
typedef struct {
    music_cmd_type_t cmd;
    int              param1;     /* 命令参数(曲目索引/音量) */
} music_cmd_t;

/* ---- 公共接口 ---- */

/* 创建队列 + 任务(在 Monitor_Start 中调用) */
void MusicTask_Start(void);

/* 发送命令(非阻塞,timeout=0)
 * 返回: 1 成功, 0 队列满 */
int MusicTask_SendCmd(music_cmd_type_t cmd, int param1, TickType_t timeout);

/* 查询命令队列水位(供 SYSMONITOR 显示) */
int MusicTask_GetCmdWatermark(void);

/* 查询任务栈剩余(字,供 SYSMONITOR 显示) */
uint32_t MusicTask_GetStackWatermark(void);

/* 查询任务运行状态(供 SYSMONITOR 显示)
 * 返回 eTaskState: eRunning/eReady/eBlocked/eSuspended/eDeleted */
eTaskState MusicTask_GetState(void);

#endif /* MUSIC_TASK_H */
