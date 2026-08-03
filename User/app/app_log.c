#include "app_log.h"
#include "app_manager.h"
#include "log_store.h"
#include "gui.h"
#include "lcd.h"
#include "monitor.h"
#include <stddef.h>
#include <string.h>

/* ============================================================
 * 日志查看应用
 *
 * 显示日志列表（最新在上）
 * 上下方向键翻页
 * OK 键清空日志
 * BACK 返回桌面
 * ============================================================ */

/* 颜色 */
#define CLR_BG          BLACK
#define CLR_TITLE       CYAN
#define CLR_TIME        WHITE
#define CLR_TYPE        YELLOW
#define CLR_TEXT        GREEN
#define CLR_SEL         YELLOW
#define CLR_HINT        GRAY
#define CLR_ERROR       RED

/* 布局 */
#define SCR_W   480
#define SCR_H   320
#define ROW_H   20                  /* 每行高度 */
#define TITLE_Y 15
#define LINE_Y  40                  /* 分隔线 */
#define LIST_Y  50                  /* 列表起始 Y */
#define HINT_Y  (SCR_H - 25)        /* 底部提示 */

/* 每页显示行数 */
#define ROWS_PER_PAGE    10

/* 各列 X 坐标 */
#define COL_TIME     15
#define COL_TYPE     85
#define COL_TEXT     145

/* ---- 模块状态 ---- */
static int need_redraw;
static int scroll_offset;       /* 滚动偏移（0=最新） */
static key_state_t prev_key;
static int last_count;          /* 上次显示的日志数（检测新增） */

/* ---- 时间格式化 HH:MM:SS ---- */
static void format_time(uint32_t sec, char *buf)
{
    uint32_t h = sec / 3600;
    uint32_t m = (sec % 3600) / 60;
    uint32_t s = sec % 60;
    buf[0] = '0' + (h / 10);
    buf[1] = '0' + (h % 10);
    buf[2] = ':';
    buf[3] = '0' + (m / 10);
    buf[4] = '0' + (m % 10);
    buf[5] = ':';
    buf[6] = '0' + (s / 10);
    buf[7] = '0' + (s % 10);
    buf[8] = 0;
}

/* ---- 画标题 + 计数 ---- */
static void draw_title(int count)
{
    char buf[32];
    LCD_Fill(0, TITLE_Y - 5, SCR_W, LINE_Y, CLR_BG);
    GUI_DrawString(15, TITLE_Y, "LOG VIEWER", CLR_TITLE, CLR_BG, 2);

    /* 右上角显示计数 */
    /* 简单拼接 "[xx/32]" */
    GUI_DrawString(SCR_W - 90, TITLE_Y + 5, "[", CLR_HINT, CLR_BG, 1);
    GUI_DrawNum(SCR_W - 82, TITLE_Y + 5, count, CLR_HINT, CLR_BG, 1);
    GUI_DrawString(SCR_W - 55, TITLE_Y + 5, "/", CLR_HINT, CLR_BG, 1);
    GUI_DrawNum(SCR_W - 47, TITLE_Y + 5, LOG_MAX_ENTRIES, CLR_HINT, CLR_BG, 1);
    GUI_DrawString(SCR_W - 15, TITLE_Y + 5, "]", CLR_HINT, CLR_BG, 1);

    /* 分隔线 */
    LCD_Fill(15, LINE_Y, SCR_W - 15, LINE_Y + 1, CLR_TITLE);
}

/* ---- 画一行日志 ---- */
static void draw_row(int row, int log_idx)
{
    uint16_t y = LIST_Y + row * ROW_H;
    const log_entry_t *e = LogStore_Get(log_idx);

    /* 清除该行 */
    LCD_Fill(0, y, SCR_W - 1, y + ROW_H - 1, CLR_BG);

    if (e == NULL)
        return;

    /* 时间 */
    {
        char tbuf[16];
        format_time(e->timestamp, tbuf);
        GUI_DrawString(COL_TIME, y + 4, tbuf, CLR_TIME, CLR_BG, 1);
    }

    /* 类型 */
    GUI_DrawString(COL_TYPE, y + 4, LogStore_TypeStr(e->type),
                   (e->type == LOG_TYPE_ERROR) ? CLR_ERROR : CLR_TYPE, CLR_BG, 1);

    /* 文本 */
    GUI_DrawString(COL_TEXT, y + 4, e->text, CLR_TEXT, CLR_BG, 1);
}

/* ---- 全屏重绘 ---- */
static void redraw_all(void)
{
    int i;
    int count = LogStore_GetCount();

    LCD_Clear(CLR_BG);
    draw_title(count);

    /* 画日志列表 */
    for (i = 0; i < ROWS_PER_PAGE; i++)
    {
        int idx = scroll_offset + i;
        if (idx >= count) break;
        draw_row(i, idx);
    }

    /* 底部提示 */
    GUI_DrawString(15, HINT_Y, "[UP/DN] Scroll  [OK]Clear  [BACK]Exit",
                   CLR_HINT, CLR_BG, 1);
}

/* ============================================================
 * 应用入口
 * ============================================================ */

void app_log_create(void)
{
}

void app_log_start(void)
{
    need_redraw = 1;
    scroll_offset = 0;
    memset(&prev_key, 0, sizeof(prev_key));
    last_count = -1;
}

void app_log_run(key_state_t *key)
{
    uint8_t e_up   = (!prev_key.up)   && key->up;
    uint8_t e_down = (!prev_key.down) && key->down;
    uint8_t e_ok   = (!prev_key.ok)   && key->ok;
    uint8_t e_back = (!prev_key.back) && key->back;
    int count = LogStore_GetCount();

    /* BACK: 返回桌面 */
    if (e_back)
    {
        AppManager_GotoDesktop();
        prev_key = *key;
        return;
    }

    /* 输入事件计数 */
    if (e_up || e_down || e_ok)
        Monitor_IncInputEvent();

    /* 首次进入或从暂停恢复：全屏重绘 */
    if (need_redraw)
    {
        redraw_all();
        need_redraw = 0;
        last_count = count;
        prev_key = *key;
        return;
    }

    /* 检测是否有新日志（自动刷新） */
    if (count != last_count)
    {
        redraw_all();
        last_count = count;
        prev_key = *key;
        return;
    }

    /* 上翻（看更旧的） */
    if (e_up)
    {
        if (scroll_offset + ROWS_PER_PAGE < count)
        {
            scroll_offset++;
            redraw_all();
        }
    }

    /* 下翻（看更新的） */
    if (e_down)
    {
        if (scroll_offset > 0)
        {
            scroll_offset--;
            redraw_all();
        }
    }

    /* OK 键：清空日志 */
    if (e_ok)
    {
        LogStore_Clear();
        scroll_offset = 0;
        redraw_all();
    }

    prev_key = *key;
}

void app_log_pause(void)
{
    need_redraw = 1;
}
