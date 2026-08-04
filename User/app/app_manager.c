#include "app_manager.h"
#include "desktop.h"
#include "app_registry.h"
#include "monitor.h"
#include "app_draw.h"
#include "usart.h"
#include <stddef.h>
#include <string.h>

/* ---- 模块状态 ---- */
static rt_state_t rt_state;
static int current_app;     /* 当前前台应用索引，-1=无 */
static app_state_t *app_states;  /* 各应用的状态（指针指向注册表内部） */

/* 最小化应用列表：存储被最小化的应用注册表索引，-1=空 */
static int minimized_list[MAX_MINIMIZED];

/* AppManager 自身的按键状态（用于检测编码器按压边沿） */
static key_state_t am_prev_key;

void AppManager_Init(void)
{
    int i;
    int count = AppRegistry_GetCount();

    rt_state = RT_DESKTOP;
    current_app = -1;

    /* 初始化所有应用状态为 NONE */
    app_states = AppRegistry_GetStates();
    for (i = 0; i < count; i++)
    {
        app_states[i] = APP_STATE_NONE;
    }

    /* 清空最小化列表 */
    for (i = 0; i < MAX_MINIMIZED; i++)
        minimized_list[i] = -1;

    memset(&am_prev_key, 0, sizeof(am_prev_key));

    /* 初始化桌面 */
    Desktop_Init();
}

rt_state_t AppManager_GetState(void)
{
    return rt_state;
}

int AppManager_GetCurrentApp(void)
{
    return current_app;
}

void AppManager_GotoDesktop(void)
{
    /* 彻底关闭当前应用：调用 on_pause 停止后台活动，状态置 NONE */
    if (current_app >= 0)
    {
        const app_entry_t *app = AppRegistry_GetApp(current_app);
        if (app && app->on_pause) app->on_pause();
        app_states[current_app] = APP_STATE_NONE;

        /* 从最小化列表中移除（如果存在） */
        {
            int i;
            for (i = 0; i < MAX_MINIMIZED; i++)
            {
                if (minimized_list[i] == current_app)
                {
                    /* 后面的元素前移 */
                    int j;
                    for (j = i; j < MAX_MINIMIZED - 1; j++)
                        minimized_list[j] = minimized_list[j + 1];
                    minimized_list[MAX_MINIMIZED - 1] = -1;
                    break;
                }
            }
        }
    }

    current_app = -1;
    rt_state = RT_DESKTOP;
    Monitor_SetState(SYS_STATE_DESKTOP);

    /* 恢复桌面显示 */
    Desktop_Resume();
}

int AppManager_MinimizeCurrent(void)
{
    int slot, i;

    if (current_app < 0) return -1;

    /* 检查是否已在最小化列表中（避免重复） */
    for (i = 0; i < MAX_MINIMIZED; i++)
        if (minimized_list[i] == current_app) return -1;

    /* 找空槽 */
    slot = -1;
    for (i = 0; i < MAX_MINIMIZED; i++)
    {
        if (minimized_list[i] < 0) { slot = i; break; }
    }
    if (slot < 0) return -1;  /* 任务栏已满 */

    /* 调用 on_pause 保存状态（音乐等独立任务应用在此决定是否继续后台运行） */
    {
        const app_entry_t *app = AppRegistry_GetApp(current_app);
        if (app && app->on_pause) app->on_pause();
        app_states[current_app] = APP_STATE_PAUSED;
    }

    minimized_list[slot] = current_app;
    current_app = -1;
    rt_state = RT_DESKTOP;
    Monitor_SetState(SYS_STATE_DESKTOP);

    Log_Printf("[APP] minimize -> slot %d\r\n", slot);

    Desktop_Resume();
    return 0;
}

int AppManager_RestoreMinimized(int slot)
{
    int app_idx;
    const app_entry_t *app;

    if (slot < 0 || slot >= MAX_MINIMIZED) return -1;
    app_idx = minimized_list[slot];
    if (app_idx < 0) return -1;

    /* 从最小化列表移除，后面的元素前移 */
    {
        int j;
        for (j = slot; j < MAX_MINIMIZED - 1; j++)
            minimized_list[j] = minimized_list[j + 1];
        minimized_list[MAX_MINIMIZED - 1] = -1;
    }

    app = AppRegistry_GetApp(app_idx);
    if (!app) return -1;

    /* 从暂停恢复：调用 on_start */
    if (app->on_start) app->on_start();
    app_states[app_idx] = APP_STATE_RUNNING;
    current_app = app_idx;
    rt_state = RT_APP;
    Monitor_SetState(SYS_STATE_APP);

    Log_Printf("[APP] restore from slot %d -> app %d\r\n", slot, app_idx);
    return 0;
}

int AppManager_GetMinimizedCount(void)
{
    int i, cnt = 0;
    for (i = 0; i < MAX_MINIMIZED; i++)
        if (minimized_list[i] >= 0) cnt++;
    return cnt;
}

int AppManager_GetMinimizedApp(int slot)
{
    if (slot < 0 || slot >= MAX_MINIMIZED) return -1;
    return minimized_list[slot];
}

int AppManager_StartApp(int idx)
{
    const app_entry_t *app;
    int count = AppRegistry_GetCount();
    int i;

    if (idx < 0 || idx >= count) return -1;

    app = AppRegistry_GetApp(idx);

    /* 如果该应用已在最小化列表中，先移除（避免任务栏残留幽灵图标） */
    for (i = 0; i < MAX_MINIMIZED; i++)
    {
        if (minimized_list[i] == idx)
        {
            int j;
            for (j = i; j < MAX_MINIMIZED - 1; j++)
                minimized_list[j] = minimized_list[j + 1];
            minimized_list[MAX_MINIMIZED - 1] = -1;
            break;
        }
    }

    /* 首次启动：调用 on_create */
    if (app_states[idx] == APP_STATE_NONE)
    {
        if (app->on_create) app->on_create();
    }

    /* 从暂停恢复：调用 on_start */
    if (app->on_start) app->on_start();

    app_states[idx] = APP_STATE_RUNNING;
    current_app = idx;
    rt_state = RT_APP;
    Monitor_SetState(SYS_STATE_APP);

    return 0;
}

void AppManager_Run(key_state_t *key)
{
    /* 编码器按压边沿：在应用内时触发最小化 */
    uint8_t e_ecsw = (!am_prev_key.ec_sw) && key->ec_sw;

    if (rt_state == RT_APP && e_ecsw)
    {
        AppManager_MinimizeCurrent();
        am_prev_key = *key;
        return;  /* 不传给应用，避免误触发 */
    }
    am_prev_key = *key;

    if (rt_state == RT_DESKTOP)
    {
        /* 检查是否有从 FILE 应用触发的绘图打开请求 */
        if (app_draw_is_open_requested())
        {
            /* DRAW 在注册表中的索引为 1 */
            AppManager_StartApp(1);
            return;
        }

        /* 桌面运行：处理光标移动、图标选中、任务栏交互 */
        int sel = Desktop_Run(key);
        if (sel >= 0)
        {
            /* Desktop_Run 返回值：
             *   0~MAX_MINIMIZED-1 = 恢复最小化应用(任务栏)
             *   MAX_MINIMIZED+    = 启动新应用(桌面图标,值为 app_idx + MAX_MINIMIZED) */
            if (sel < MAX_MINIMIZED)
                AppManager_RestoreMinimized(sel);
            else
                AppManager_StartApp(sel - MAX_MINIMIZED);
        }
    }
    else if (rt_state == RT_APP)
    {
        /* 调用应用的 on_run，由应用自己处理 BACK 键
         * - 单状态应用：BACK 边沿 → AppManager_GotoDesktop()
         * - 多状态应用（如 FILE）：BACK 先返回上层界面，到顶层再退出 */
        if (current_app >= 0)
        {
            const app_entry_t *app = AppRegistry_GetApp(current_app);
            if (app->on_run) app->on_run(key);
        }
    }
}

int AppManager_GetAppCount(void)
{
    return AppRegistry_GetCount();
}

const app_entry_t *AppManager_GetApp(int idx)
{
    return AppRegistry_GetApp(idx);
}
