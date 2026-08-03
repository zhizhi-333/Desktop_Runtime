#include "app_registry.h"
#include "lcd.h"
#include "gui.h"
#include "app_sysmonitor.h"
#include "app_settings.h"
#include "app_log.h"
#include "app_draw.h"
#include "app_file.h"
#include "app_music.h"
#include "app_settime.h"
#include <stddef.h>

/* ============================================================
 * 应用注册表
 *
 * 6 个应用（按题目要求）：
 *   1. 文件管理 - 文件列表/新建/删除/重名/读写/掉电保持
 *   2. 画图     - 绘图/清空/保存/重新打开/掉电保持
 *   3. 音乐播放 - 旋律播放/音量关联/退出处理
 *   4. 日志查看 - 显示系统日志/事件记录/清空日志
 *   5. 系统监控 - 运行时间/状态/事件计数/栈水位/内存
 *   6. 系统设置 - 光标灵敏度/大小/亮度/音量/熄屏时间/掉电保持
 *
 * 新增应用只需：
 *   1. 实现 on_create/on_start/on_run/on_pause
 *   2. 在 apps 数组中添加一条
 * ============================================================ */

/* ---- 1. 文件管理（已实现） ---- */
/* 入口函数定义在 app_file.c，这里直接引用 */

/* ---- 2. 画图（已实现） ---- */
/* 入口函数定义在 app_draw.c，这里直接引用 */

/* ---- 3. 音乐播放（已实现） ---- */
/* 入口函数定义在 app_music.c，这里直接引用 */

/* ---- 4. 日志查看（已实现） ---- */
/* 入口函数定义在 app_log.c，这里直接引用 */

/* ---- 5. 系统监控（已实现） ---- */
/* 入口函数定义在 app_sysmonitor.c，这里直接引用 */

/* ---- 6. 系统设置（已实现） ---- */
/* 入口函数定义在 app_settings.c，这里直接引用 */

/* ---- 7. 时间设置（已实现） ---- */
/* 入口函数定义在 app_settime.c，这里直接引用 */

/* ---- 应用注册表 ---- */
static const app_entry_t apps[] = {
    { "FILE",    app_file_create,    app_file_start,    app_file_run,    app_file_pause    },
    /* 注：app_file_* 定义在 app_file.c */
    { "DRAW",    app_draw_create,    app_draw_start,    app_draw_run,    app_draw_pause    },
    /* 注：app_draw_* 定义在 app_draw.c */
    { "MUSIC",   app_music_create,   app_music_start,   app_music_run,   app_music_pause   },
    /* 注：app_music_* 定义在 app_music.c */
    { "LOG",     app_log_create,     app_log_start,     app_log_run,     app_log_pause     },
    /* 注：app_log_* 定义在 app_log.c */
    { "MONITOR", app_sysmonitor_create, app_sysmonitor_start, app_sysmonitor_run, app_sysmonitor_pause },
    { "CONFIG",  app_setting_create, app_setting_start, app_setting_run, app_setting_pause },
    { "SETTIME", app_settime_create, app_settime_start, app_settime_run, app_settime_pause },
};

static app_state_t app_states[sizeof(apps) / sizeof(apps[0])];

int AppRegistry_GetCount(void)
{
    return (int)(sizeof(apps) / sizeof(apps[0]));
}

const app_entry_t *AppRegistry_GetApp(int idx)
{
    if (idx < 0 || idx >= AppRegistry_GetCount()) return NULL;
    return &apps[idx];
}

app_state_t *AppRegistry_GetStates(void)
{
    return app_states;
}
