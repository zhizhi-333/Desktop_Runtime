#include "app_settime.h"
#include "app_manager.h"
#include "gui.h"
#include "lcd.h"
#include "rtc_time.h"
#include "monitor.h"
#include "log_store.h"
#include "usart.h"
#include <stddef.h>
#include <stdio.h>

/* ============================================================
 * 时间设置应用
 *
 * 界面：
 *   SET TIME
 *   ┌──────────────┐
 *   │  HH : MM : SS │   ←/→ 切换选中位
 *   └──────────────┘   ↑/↓ 调整数值
 *
 *   [OK]Save  [BACK]Exit
 *
 * 操作：
 *   ←/→   切换选中位(时/分/秒)
 *   ↑/↓   调整数值
 *   OK    保存并返回
 *   BACK  不保存返回
 * ============================================================ */

#define SCR_W   480
#define SCR_H   320

/* 颜色 */
#define CLR_BG      BLACK
#define CLR_TITLE   CYAN
#define CLR_TEXT    WHITE
#define CLR_SEL     YELLOW
#define CLR_HINT    GRAY
#define CLR_OK      GREEN

/* 布局 */
#define TIME_Y      130
#define TIME_X0     130
#define DIGIT_W     60      /* 每段数字宽度 */
#define COLON_W     20      /* 冒号宽度 */
#define CURSOR_W    50      /* 选中框宽度 */
#define CURSOR_H    50

/* 模块状态 */
static int need_redraw;
static int sel_field;       /* 0=时 1=分 2=秒 */
static uint8_t edit_h, edit_m, edit_s;
static key_state_t prev_key;

static void draw_time_display(void)
{
    char buf[16];
    int i;
    uint16_t x;

    /* 清除时间显示区 */
    LCD_Fill(TIME_X0 - 10, TIME_Y - 10, TIME_X0 + 3 * DIGIT_W + 2 * COLON_W + 10,
             TIME_Y + CURSOR_H + 10, CLR_BG);

    /* 三段数字 + 两个冒号 */
    for (i = 0; i < 3; i++)
    {
        x = TIME_X0 + i * (DIGIT_W + COLON_W);
        /* 选中框 */
        if (i == sel_field)
            GUI_DrawRect(x, TIME_Y, CURSOR_W, CURSOR_H, CLR_SEL);

        /* 数字 */
        switch (i)
        {
            case 0: snprintf(buf, sizeof(buf), "%02d", edit_h); break;
            case 1: snprintf(buf, sizeof(buf), "%02d", edit_m); break;
            case 2: snprintf(buf, sizeof(buf), "%02d", edit_s); break;
        }
        {
            uint16_t w = GUI_StringWidth(buf, 3);
            GUI_DrawString(x + (CURSOR_W - w) / 2, TIME_Y + 8, buf,
                           (i == sel_field) ? CLR_SEL : CLR_TEXT, CLR_BG, 3);
        }

        /* 冒号（最后一个不画） */
        if (i < 2)
            GUI_DrawString(x + DIGIT_W, TIME_Y + 8, ":", CLR_TEXT, CLR_BG, 3);
    }
}

static void redraw_all(void)
{
    LCD_Clear(CLR_BG);

    /* 标题 */
    {
        const char *t = "SET TIME";
        uint16_t w = GUI_StringWidth(t, 2);
        GUI_DrawString((SCR_W - w) / 2, 40, t, CLR_TITLE, CLR_BG, 2);
    }

    /* 时间显示 */
    draw_time_display();

    /* 选中位标签 */
    {
        const char *labels[] = { "Hour", "Minute", "Second" };
        const char *lab = labels[sel_field];
        uint16_t w = GUI_StringWidth(lab, 1);
        GUI_DrawString((SCR_W - w) / 2, TIME_Y + CURSOR_H + 20, lab,
                       CLR_SEL, CLR_BG, 1);
    }

    /* 操作提示 */
    GUI_DrawString(60, SCR_H - 30, "L/R:Select  U/D:Adjust", CLR_HINT, CLR_BG, 1);
    GUI_DrawString(60, SCR_H - 16, "[OK]Save  [BACK]Exit", CLR_HINT, CLR_BG, 1);
}

void app_settime_create(void)
{
    /* 应用创建时无需特殊初始化 */
}

void app_settime_start(void)
{
    /* 进入时读取当前时间 */
    edit_h = RTC_GetHour();
    edit_m = RTC_GetMinute();
    edit_s = RTC_GetSecond();
    sel_field = 0;
    need_redraw = 1;
}

void app_settime_run(key_state_t *key)
{
    uint8_t e_left  = (!prev_key.left)  && key->left;
    uint8_t e_right = (!prev_key.right) && key->right;
    uint8_t e_up    = (!prev_key.up)    && key->up;
    uint8_t e_down  = (!prev_key.down)  && key->down;
    uint8_t e_ok    = (!prev_key.ok)    && key->ok;
    uint8_t e_back  = (!prev_key.back)  && key->back;

    if (e_back)
    {
        AppManager_GotoDesktop();
        prev_key = *key;
        return;
    }

    if (e_left || e_right || e_up || e_down || e_ok)
        Monitor_IncInputEvent();

    if (need_redraw)
    {
        redraw_all();
        need_redraw = 0;
        prev_key = *key;
        return;
    }

    /* 切换选中位 */
    if (e_left)
    {
        if (sel_field > 0) sel_field--;
        else sel_field = 2;
        need_redraw = 1;
    }
    if (e_right)
    {
        if (sel_field < 2) sel_field++;
        else sel_field = 0;
        need_redraw = 1;
    }

    /* 调整数值 */
    if (e_up || e_down)
    {
        int step = e_up ? 1 : -1;
        switch (sel_field)
        {
            case 0:  /* 时 */
                edit_h = (uint8_t)((edit_h + step + 24) % 24);
                break;
            case 1:  /* 分 */
                edit_m = (uint8_t)((edit_m + step + 60) % 60);
                break;
            case 2:  /* 秒 */
                edit_s = (uint8_t)((edit_s + step + 60) % 60);
                break;
        }
        need_redraw = 1;
    }

    /* OK: 保存并返回 */
    if (e_ok)
    {
        RTC_SetTime(edit_h, edit_m, edit_s);
        RTC_Save();   /* 保存到 Flash */
        Log_Printf("[SETTIME] saved %02d:%02d:%02d\r\n", edit_h, edit_m, edit_s);
        AppManager_GotoDesktop();
        prev_key = *key;
        return;
    }

    prev_key = *key;
}

void app_settime_pause(void)
{
    need_redraw = 1;
}
