#include "app_manager.h"
#include "desktop.h"
#include "app_registry.h"
#include "monitor.h"
#include <stddef.h>

/* ---- 模块状态 ---- */
static rt_state_t rt_state;
static int current_app;     /* 当前前台应用索引，-1=无 */
static app_state_t *app_states;  /* 各应用的状态（指针指向注册表内部） */

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
    /* 当前应用暂停（保留状态） */
    if (current_app >= 0 && app_states[current_app] == APP_STATE_RUNNING)
    {
        const app_entry_t *app = AppRegistry_GetApp(current_app);
        if (app->on_pause) app->on_pause();
        app_states[current_app] = APP_STATE_PAUSED;
    }

    current_app = -1;
    rt_state = RT_DESKTOP;
    Monitor_SetState(SYS_STATE_DESKTOP);

    /* 恢复桌面显示 */
    Desktop_Resume();
}

int AppManager_StartApp(int idx)
{
    const app_entry_t *app;
    int count = AppRegistry_GetCount();

    if (idx < 0 || idx >= count) return -1;

    app = AppRegistry_GetApp(idx);

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
    if (rt_state == RT_DESKTOP)
    {
        /* 桌面运行：处理光标移动、图标选中 */
        int sel = Desktop_Run(key);
        if (sel >= 0)
        {
            /* 用户选中了一个应用，启动它 */
            AppManager_StartApp(sel);
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
