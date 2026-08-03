#include "desktop.h"
#include "app_manager.h"
#include "gui.h"
#include "lcd.h"
#include "settings.h"
#include "rtc_time.h"
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

/* ---- 状态栏 ---- */
#define STATUS_Y        295

/* ---- 模块状态 ---- */
static int16_t cursor_x, cursor_y;
static int16_t prev_cx, prev_cy;
static key_state_t prev_key;
static int need_redraw;
static int last_sec = -1;           /* 上次显示的秒数(检测变化) */
static int last_id_conn = -1;       /* 上次输入设备状态 */

/* ---- 获取图标位置 ---- */
static void get_icon_pos(int idx, uint16_t *x, uint16_t *y)
{
    int col = idx % ICON_COLS;
    int row = idx / ICON_COLS;
    *x = ICON_X0 + col * (ICON_W + ICON_GAP_X);
    *y = ICON_Y0 + row * (ICON_H + ICON_GAP_Y);
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
}

/* ---- 画状态栏 ---- */
/* 布局: [8] +cursor  [120] TIME: HH:MM:SS  [280] INPUT: OK/NC  [380] APP: NAME
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
            GUI_DrawString(280, STATUS_Y, "INPUT: OK", CLR_STATUS, CLR_BG, 1);
        else
            GUI_DrawString(280, STATUS_Y, "INPUT: NC", YELLOW, CLR_BG, 1);
    }

    /* 时间区域局部刷新（x=120 到 x=230，避免清除其他元素） */
    LCD_Fill(120, STATUS_Y, 230, SCR_H - 1, CLR_BG);
    snprintf(buf, sizeof(buf), "TIME: %02d:%02d:%02d",
             RTC_GetHour(), RTC_GetMinute(), RTC_GetSecond());
    GUI_DrawString(120, STATUS_Y, buf, CLR_STATUS, CLR_BG, 1);
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
        prev_key = *key;
        return -1;
    }

    /* 状态栏定期刷新：
     * - 设备状态变化 -> 全量重绘(full=1)
     * - 仅秒数变化 -> 局部刷新时间区域(full=0)，避免整行闪烁 */
    {
        int cur_sec = RTC_GetSecond();
        int cur_id = key->id_connected;
        if (cur_id != last_id_conn)
        {
            prev_key = *key;  /* 让 draw_status_bar 读到新的 id_connected */
            draw_status_bar(1);
            last_sec = cur_sec;
            last_id_conn = cur_id;
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

    /* 光标移动 */
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

        restore_cursor_area(prev_cx, prev_cy);
        draw_cursor(cursor_x, cursor_y);
        prev_cx = cursor_x;
        prev_cy = cursor_y;
    }

    /* OK 键：选中图标启动应用 */
    if (e_ok)
    {
        if (cursor_on_icon(&new_sel))
        {
            prev_key = *key;
            return new_sel;   /* 通知 AppManager 启动应用 */
        }
    }

    prev_key = *key;
    return -1;
}
