#include "desktop.h"
#include "app_manager.h"
#include "gui.h"
#include "lcd.h"
#include "settings.h"
#include "rtc_time.h"
#include "monitor.h"
#include "FreeRTOS.h"
#include "task.h"
#include <stddef.h>
#include <string.h>
#include <stdio.h>

/* ============================================================
 * 桌面环境 (480x320 横屏)
 *
 *   ┌──────────────────────────────────┐
 *   │ DESKTOP                          │  y=8, 标题
 *   │                                  │
 *   │  ┌────┐  ┌────┐  ┌────┐  ┌────┐ │  y=50, 图标网格 4x2
 *   │  │ 1  │  │ 2  │  │ 3  │  │ 4  │ │  每个 90x70, 间距 12
 *   │  └────┘  └────┘  └────┘  └────┘ │
 *   │  ┌────┐  ┌────┐                 │
 *   │  │ 5  │  │ 6  │                 │
 *   │  └────┘  └────┘                 │
 *   │                                  │
 *   │  + cursor    APP: SCOPE          │  y=290, 状态栏
 *   └──────────────────────────────────┘
 * ============================================================ */

#define SCR_W   480
#define SCR_H   320

/* ---- 图标布局 ---- */
#define ICON_W          90
#define ICON_H          70
#define ICON_GAP_X      12
#define ICON_GAP_Y      12
#define ICON_X0         ((SCR_W - 4 * ICON_W - 3 * ICON_GAP_X) / 2)  /* 42 */
#define ICON_Y0         50
#define ICON_COLS       4

/* ---- 光标 ----（从设置模块读取，支持运行时调整） */
#define CURSOR_R        Settings_CursorSize()
#define CURSOR_COLOR    YELLOW
#define CURSOR_STEP     Settings_CursorSensitivity()

/* ---- 颜色 ---- */
#define CLR_BG          BLACK
#define CLR_TITLE       CYAN
#define CLR_ICON_BG     GRAY
#define CLR_ICON_FG     WHITE
#define CLR_ICON_SEL    BLUE
#define CLR_ICON_BORDER WHITE
#define CLR_STATUS      GREEN
#define CLR_TASKBAR_BG  BLACK
#define CLR_TASKBAR_ICON_BG GRAY
#define CLR_TASKBAR_ICON_SEL BLUE
#define CLR_TASKBAR_ICON_FG  WHITE

/* ---- 状态栏 ---- */
#define STATUS_Y        295

/* ---- 任务栏（最小化应用图标区，状态栏上方） ---- */
#define TASKBAR_Y       275
#define TASKBAR_H       20
#define TASKBAR_ICON_W  70
#define TASKBAR_ICON_X0 10
#define TASKBAR_ICON_GAP 5

/* ---- 模块状态 ---- */
static int16_t cursor_x, cursor_y;
static int16_t prev_cx, prev_cy;
static key_state_t prev_key;
static int need_redraw;
static int last_sec = -1;           /* 上次显示的秒数(检测变化) */
static int last_id_conn = -1;       /* 上次输入设备状态 */
static uint32_t last_err_flags = 0xFFFFFFFF; /* 上次错误标志(初始值故意不同以触发首次刷新) */
static int last_minimized_cnt = -1; /* 上次最小化应用数(检测任务栏变化) */

/* ---- 拖动状态 ---- */
static int drag_active = 0;         /* 是否正在拖动图标 */
static int drag_icon_idx = -1;      /* 被拖动的图标索引 */
static int drag_moved = 0;          /* 拖动期间是否发生过移动(用于区分点击vs拖动) */
static int16_t drag_off_x, drag_off_y; /* 按下时光标相对图标左上角的偏移 */

/* ---- 图标位置表（支持拖动后改变位置） ---- */
#define MAX_ICONS 16
static int16_t icon_pos_x[MAX_ICONS];
static int16_t icon_pos_y[MAX_ICONS];
static int icon_pos_initialized = 0;

/* ---- 获取图标位置 ---- */
static void get_icon_pos(int idx, uint16_t *x, uint16_t *y)
{
    if (idx < 0 || idx >= MAX_ICONS)
    {
        *x = 0; *y = 0;
        return;
    }
    /* 首次访问时用网格位置初始化 */
    if (!icon_pos_initialized)
    {
        int i;
        for (i = 0; i < MAX_ICONS; i++)
        {
            int col = i % ICON_COLS;
            int row = i / ICON_COLS;
            icon_pos_x[i] = ICON_X0 + col * (ICON_W + ICON_GAP_X);
            icon_pos_y[i] = ICON_Y0 + row * (ICON_H + ICON_GAP_Y);
        }
        icon_pos_initialized = 1;
    }
    *x = icon_pos_x[idx];
    *y = icon_pos_y[idx];
}

/* ---- 设置图标位置（拖动时调用，带边界限制） ---- */
static void set_icon_pos(int idx, int16_t x, int16_t y)
{
    if (idx < 0 || idx >= MAX_ICONS) return;
    /* 限制图标在桌面区域内（不能压到标题和状态栏） */
    if (x < 0) x = 0;
    if (y < 30) y = 30;  /* 避开顶部标题区 */
    if (x + ICON_W > SCR_W) x = SCR_W - ICON_W;
    if (y + ICON_H > TASKBAR_Y - 1) y = TASKBAR_Y - 1 - ICON_H;
    icon_pos_x[idx] = x;
    icon_pos_y[idx] = y;
}

/* ---- 画单个图标 ---- */
static void draw_icon(int idx, int selected)
{
    uint16_t x, y;
    const app_entry_t *app;
    uint16_t bg = selected ? CLR_ICON_SEL : CLR_ICON_BG;

    get_icon_pos(idx, &x, &y);
    app = AppManager_GetApp(idx);
    if (!app) return;

    /* 填充背景 + 边框 */
    LCD_Fill(x, y, x + ICON_W - 1, y + ICON_H - 1, bg);
    GUI_DrawRect(x, y, ICON_W, ICON_H, CLR_ICON_BORDER);

    /* 应用名称（居中，scale=1） */
    {
        const char *name = app->name;
        uint16_t w = GUI_StringWidth(name, 1);
        uint16_t tx = x + (ICON_W - w) / 2;
        uint16_t ty = y + (ICON_H - 7) / 2;
        GUI_DrawString(tx, ty, name, CLR_ICON_FG, bg, 1);
    }
}

/* 前向声明 */
static void draw_taskbar(void);

/* ---- 画整个桌面 ---- */
static void draw_desktop(void)
{
    int i, count;

    LCD_Clear(CLR_BG);

    /* 标题 */
    GUI_DrawString(8, 8, "DESKTOP", CLR_TITLE, CLR_BG, 2);

    /* 图标网格 */
    count = AppManager_GetAppCount();
    for (i = 0; i < count; i++)
        draw_icon(i, 0);

    /* 任务栏 */
    draw_taskbar();
}

/* ---- 画任务栏（最小化应用图标区） ---- */
static void draw_taskbar(void)
{
    int i, cnt;

    cnt = AppManager_GetMinimizedCount();

    /* 清除任务栏区域 */
    LCD_Fill(0, TASKBAR_Y, SCR_W - 1, STATUS_Y - 1, CLR_TASKBAR_BG);

    /* 顶部分隔线 */
    LCD_Fill(0, TASKBAR_Y, SCR_W - 1, TASKBAR_Y, CLR_TITLE);

    /* 无最小化应用时不画图标，但显示提示 */
    if (cnt == 0)
    {
        GUI_DrawString(180, TASKBAR_Y + 4, "[taskbar empty]", GRAY, CLR_TASKBAR_BG, 1);
        return;
    }

    /* 画每个最小化应用图标 */
    for (i = 0; i < MAX_MINIMIZED; i++)
    {
        int app_idx = AppManager_GetMinimizedApp(i);
        if (app_idx >= 0)
        {
            const app_entry_t *app = AppManager_GetApp(app_idx);
            uint16_t x = TASKBAR_ICON_X0 + i * (TASKBAR_ICON_W + TASKBAR_ICON_GAP);
            uint16_t bg = CLR_TASKBAR_ICON_BG;
            char label[16];

            /* 图标背景 + 边框 */
            LCD_Fill(x, TASKBAR_Y + 2, x + TASKBAR_ICON_W - 1, STATUS_Y - 2, bg);
            GUI_DrawRect(x, TASKBAR_Y + 2, TASKBAR_ICON_W, TASKBAR_H - 3, CLR_ICON_BORDER);

            /* 应用名（截断显示） */
            if (app)
            {
                snprintf(label, sizeof(label), "[%d]%s", i + 1, app->name);
                GUI_DrawString(x + 4, TASKBAR_Y + 5, label,
                               CLR_TASKBAR_ICON_FG, bg, 1);
            }
        }
    }
}

/* ---- 画状态栏 ---- */
/* 布局: [8] +cursor  [70] TIME: HH:MM:SS  [180] INPUT:OK/NC  [270] ERR:xxx
 * full=1: 全量重绘(首次进入/设备状态变化/光标覆盖恢复)
 * full=0: 仅局部刷新时间区域(每秒调用，避免整行闪烁) */
static void draw_status_bar(int full)
{
    char buf[32];

    if (full)
    {
        LCD_Fill(0, STATUS_Y, SCR_W - 1, SCR_H - 1, CLR_BG);
        GUI_DrawString(8, STATUS_Y, "+", CLR_STATUS, CLR_BG, 1);
        GUI_DrawString(20, STATUS_Y, "cursor", CLR_STATUS, CLR_BG, 1);

        if (prev_key.id_connected)
            GUI_DrawString(180, STATUS_Y, "INPUT:OK", CLR_STATUS, CLR_BG, 1);
        else
            GUI_DrawString(180, STATUS_Y, "INPUT:NC", YELLOW, CLR_BG, 1);
    }

    /* 时间区域局部刷新（x=70 到 x=165，避免清除其他元素） */
    LCD_Fill(70, STATUS_Y, 165, SCR_H - 1, CLR_BG);
    snprintf(buf, sizeof(buf), "%02d:%02d:%02d",
             RTC_GetHour(), RTC_GetMinute(), RTC_GetSecond());
    GUI_DrawString(70, STATUS_Y, buf, CLR_STATUS, CLR_BG, 1);

    /* 错误提示区域（x=270 到 x=470，每次刷新） */
    {
        const char *errstr = Monitor_GetErrorString();
        uint16_t errcolor = (Monitor_GetError() == SYS_ERR_NONE) ? CLR_STATUS : RED;
        LCD_Fill(270, STATUS_Y, SCR_W - 1, SCR_H - 1, CLR_BG);
        snprintf(buf, sizeof(buf), "ERR:%s", errstr);
        GUI_DrawString(270, STATUS_Y, buf, errcolor, CLR_BG, 1);
    }
}

/* ---- 画十字光标 ---- */
static void draw_cursor(int16_t cx, int16_t cy)
{
    int16_t x1 = cx - CURSOR_R, x2 = cx + CURSOR_R;
    int16_t y1 = cy - CURSOR_R, y2 = cy + CURSOR_R;
    if (x1 < 0) x1 = 0;
    if (x2 >= SCR_W) x2 = SCR_W - 1;
    if (y1 < 0) y1 = 0;
    if (y2 >= SCR_H) y2 = SCR_H - 1;
    LCD_Fill(x1, cy, x2, cy, CURSOR_COLOR);
    LCD_Fill(cx, y1, cx, y2, CURSOR_COLOR);
}

/* ---- 矩形相交检测 ---- */
static int rect_overlap(int16_t ax1, int16_t ay1, int16_t ax2, int16_t ay2,
                        int16_t bx1, int16_t by1, int16_t bx2, int16_t by2)
{
    return (ax1 <= bx2) && (ax2 >= bx1) && (ay1 <= by2) && (ay2 >= by1);
}

/* ---- 恢复光标覆盖的区域 ---- */
static void restore_cursor_area(int16_t cx, int16_t cy)
{
    int16_t x1 = cx - CURSOR_R, x2 = cx + CURSOR_R;
    int16_t y1 = cy - CURSOR_R, y2 = cy + CURSOR_R;
    int i, count;

    if (x1 < 0) x1 = 0;
    if (y1 < 0) y1 = 0;
    if (x2 >= SCR_W) x2 = SCR_W - 1;
    if (y2 >= SCR_H) y2 = SCR_H - 1;

    /* 第1步：整块填背景 */
    LCD_Fill(x1, y1, x2, y2, CLR_BG);

    /* 第2步：重绘相交元素 */

    /* 标题 */
    if (rect_overlap(x1, y1, x2, y2, 8, 8, 8 + 7*6*2 - 1, 8 + 7*2 - 1))
        GUI_DrawString(8, 8, "DESKTOP", CLR_TITLE, CLR_BG, 2);

    /* 状态栏 */
    if (rect_overlap(x1, y1, x2, y2, 0, STATUS_Y, SCR_W - 1, SCR_H - 1))
        draw_status_bar(1);

    /* 任务栏 */
    if (rect_overlap(x1, y1, x2, y2, 0, TASKBAR_Y, SCR_W - 1, STATUS_Y - 1))
        draw_taskbar();

    /* 图标 */
    count = AppManager_GetAppCount();
    for (i = 0; i < count; i++)
    {
        uint16_t ix, iy;
        get_icon_pos(i, &ix, &iy);
        if (rect_overlap(x1, y1, x2, y2, ix, iy, ix + ICON_W - 1, iy + ICON_H - 1))
            draw_icon(i, 0);
    }
}

/* ---- 检测光标在哪个图标上 ---- */
static int cursor_on_icon(int *idx)
{
    int i, count;
    count = AppManager_GetAppCount();
    for (i = 0; i < count; i++)
    {
        uint16_t ix, iy;
        get_icon_pos(i, &ix, &iy);
        if (cursor_x >= ix && cursor_x < ix + ICON_W &&
            cursor_y >= iy && cursor_y < iy + ICON_H)
        {
            *idx = i;
            return 1;
        }
    }
    return 0;
}

/* ---- 检测光标在哪个任务栏图标上 ---- */
static int cursor_on_taskbar_icon(int *slot)
{
    int i;
    for (i = 0; i < MAX_MINIMIZED; i++)
    {
        int app_idx = AppManager_GetMinimizedApp(i);
        if (app_idx >= 0)
        {
            uint16_t x = TASKBAR_ICON_X0 + i * (TASKBAR_ICON_W + TASKBAR_ICON_GAP);
            if (cursor_x >= x && cursor_x < x + TASKBAR_ICON_W &&
                cursor_y >= TASKBAR_Y + 2 && cursor_y < STATUS_Y - 1)
            {
                *slot = i;
                return 1;
            }
        }
    }
    return 0;
}

/* ============================================================ */

void Desktop_Init(void)
{
    cursor_x = ICON_X0 + ICON_W / 2;
    cursor_y = ICON_Y0 + ICON_H / 2;   /* 默认在第一个图标上 */
    memset(&prev_key, 0, sizeof(prev_key));
    need_redraw = 1;
}

void Desktop_Resume(void)
{
    /* 从应用返回，需要重绘桌面 */
    need_redraw = 1;
}

int Desktop_Run(key_state_t *key)
{
    int moved = 0;
    int old_sel = -1, new_sel = -1;

    /* 上升沿检测 */
    uint8_t e_left  = (!prev_key.left)  && key->left;
    uint8_t e_right = (!prev_key.right) && key->right;
    uint8_t e_up    = (!prev_key.up)    && key->up;
    uint8_t e_down  = (!prev_key.down)  && key->down;
    uint8_t e_ok    = (!prev_key.ok)    && key->ok;

    /* 首次进入全屏重绘 */
    if (need_redraw)
    {
        draw_desktop();
        draw_status_bar(1);
        draw_cursor(cursor_x, cursor_y);
        need_redraw = 0;
        prev_cx = cursor_x;
        prev_cy = cursor_y;
        last_sec = -1;
        last_id_conn = -1;
        last_minimized_cnt = AppManager_GetMinimizedCount();
        prev_key = *key;
        return -1;
    }

    /* 任务栏变化检测：最小化应用数量变化时重绘任务栏 */
    {
        int cur_cnt = AppManager_GetMinimizedCount();
        if (cur_cnt != last_minimized_cnt)
        {
            draw_taskbar();
            last_minimized_cnt = cur_cnt;
            draw_cursor(cursor_x, cursor_y);
        }
    }

    /* 状态栏定期刷新：
     * - 设备状态变化或错误标志变化 -> 全量重绘(full=1)
     * - 仅秒数变化 -> 局部刷新时间区域(full=0)，避免整行闪烁 */
    {
        int cur_sec = RTC_GetSecond();
        int cur_id = key->id_connected;
        uint32_t cur_err = Monitor_GetError();
        if (cur_id != last_id_conn || cur_err != last_err_flags)
        {
            prev_key = *key;  /* 让 draw_status_bar 读到新的 id_connected */
            draw_status_bar(1);
            last_sec = cur_sec;
            last_id_conn = cur_id;
            last_err_flags = cur_err;
            draw_cursor(cursor_x, cursor_y);
        }
        else if (cur_sec != last_sec)
        {
            draw_status_bar(0);
            last_sec = cur_sec;
            draw_cursor(cursor_x, cursor_y);
        }
    }

    /* 记录旧选中状态 */
    cursor_on_icon(&old_sel);

    /* ---- 拖动逻辑 ---- */
    /* OK 按下：如果在图标上，记录潜在拖动 */
    if (e_ok && !drag_active)
    {
        int sel = -1;
        if (cursor_on_icon(&sel))
        {
            uint16_t ix, iy;
            drag_active = 1;
            drag_icon_idx = sel;
            drag_moved = 0;
            get_icon_pos(sel, &ix, &iy);
            drag_off_x = cursor_x - ix;
            drag_off_y = cursor_y - iy;
        }
    }

    /* 光标移动（拖动时也响应，用于移动图标） */
    if (e_left)  { cursor_x -= CURSOR_STEP; moved = 1; }
    if (e_right) { cursor_x += CURSOR_STEP; moved = 1; }
    if (e_up)    { cursor_y -= CURSOR_STEP; moved = 1; }
    if (e_down)  { cursor_y += CURSOR_STEP; moved = 1; }

    if (moved)
    {
        if (cursor_x < 0) cursor_x = 0;
        if (cursor_y < 0) cursor_y = 0;
        if (cursor_x >= SCR_W) cursor_x = SCR_W - 1;
        if (cursor_y >= SCR_H) cursor_y = SCR_H - 1;

        /* 拖动中：移动图标位置 */
        if (drag_active && drag_icon_idx >= 0)
        {
            int16_t new_x = cursor_x - drag_off_x;
            int16_t new_y = cursor_y - drag_off_y;
            /* 擦除旧位置：先填背景再重绘 */
            uint16_t ox, oy;
            get_icon_pos(drag_icon_idx, &ox, &oy);
            LCD_Fill(ox, oy, ox + ICON_W - 1, oy + ICON_H - 1, CLR_BG);
            /* 设置新位置并重绘图标 + 光标 */
            set_icon_pos(drag_icon_idx, new_x, new_y);
            draw_icon(drag_icon_idx, 1);
            /* 标记已移动（释放时不启动应用） */
            drag_moved = 1;
        }

        restore_cursor_area(prev_cx, prev_cy);
        draw_cursor(cursor_x, cursor_y);
        prev_cx = cursor_x;
        prev_cy = cursor_y;
    }

    /* OK 释放：根据是否移动决定是启动应用还是结束拖动 */
    if (prev_key.ok && !key->ok)
    {
        if (drag_active)
        {
            if (!drag_moved)
            {
                /* 没移动 → 当作点击：启动应用 */
                new_sel = drag_icon_idx + MAX_MINIMIZED;  /* 偏移以区分任务栏 */
                drag_active = 0;
                drag_icon_idx = -1;
                prev_key = *key;
                return new_sel;   /* 通知 AppManager 启动应用 */
            }
            else
            {
                /* 移动过 → 结束拖动，保持新位置 */
                drag_active = 0;
                drag_icon_idx = -1;
            }
        }
        else
        {
            /* 非拖动状态下 OK 释放：检查是否点击了任务栏图标 */
            int tb_slot = -1;
            if (cursor_on_taskbar_icon(&tb_slot))
            {
                prev_key = *key;
                return tb_slot;  /* 返回 0~MAX_MINIMIZED-1，表示恢复最小化应用 */
            }
        }
    }

    prev_key = *key;
    return -1;
}
