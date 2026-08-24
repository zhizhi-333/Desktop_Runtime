#include "login.h"
#include "gui.h"
#include "lcd.h"
#include "settings.h"
#include "log_store.h"
#include "rtc_time.h"
#include <string.h>
#include <stdio.h>

/* ============================================================
 * 登录界面 (480x320 横屏)
 *
 * 状态1 SELECT(选择界面):
 *   ┌──────────────────────────────────┐
 *   │           LOGIN SCREEN           │  y=80, scale=2
 *   │                                  │
 *   │          ┌────────────┐          │  y=150, 120x50
 *   │          │   登 录    │          │  光标移上去按OK进入
 *   │          └────────────┘          │
 *   └──────────────────────────────────┘
 *
 * 状态2 INPUT(密码输入):
 *   ┌──────────────────────────────────┐
 *   │ LOGIN       [*][*][ ][ ]         │  y=10 标题, y=35 密码框
 *   │        [ERROR / PASS OK]         │  y=72 状态行
 *   │  ┌─┐┌─┐┌─┐┌─┐┌─┐                 │  y=90 键盘
 *   │  │1││2││3││4││5│                 │  45x40, 间距6
 *   │  └─┘└─┘└─┘└─┘└─┘                 │
 *   │  ┌─┐┌─┐┌─┐┌─┐┌─┐                 │
 *   │  │6││7││8││9││0│                 │
 *   │  └─┘└─┘└─┘└─┘└─┘                 │
 *   │       ┌─┐┌─┐┌─┐                  │  第3行3个按钮居中
 *   │       │C││<││OK│                 │
 *   │       └─┘└─┘└─┘                  │
 *   └──────────────────────────────────┘
 * ============================================================ */

#define SCR_W   480
#define SCR_H   320

/* ---- 光标 ----（从设置模块读取，支持运行时调整） */
#define CURSOR_R        Settings_CursorSize()
#define CURSOR_COLOR    YELLOW
#define CURSOR_STEP     Settings_CursorSensitivity()

/* ---- 颜色 ---- */
#define CLR_BG          BLACK
#define CLR_TITLE       CYAN
#define CLR_BTN_BG      GRAY
#define CLR_BTN_FG      WHITE
#define CLR_PIN_BORDER  WHITE
#define CLR_PIN_DOT     YELLOW
#define CLR_ERR         RED
#define CLR_OK          GREEN
#define CLR_HINT        GRAY

/* ---- 密码 ---- */
#define PIN_LEN     4
static const char correct_pin[PIN_LEN] = {'1','2','3','4'};

/* ---- 登录子状态 ---- */
typedef enum {
    LS_SELECT,  /* 选择界面：标题 + 登录按钮 */
    LS_INPUT,   /* 密码输入界面 */
    LS_ERR,     /* 密码错误提示 */
    LS_OK,      /* 密码正确 */
    LS_LOCKED,  /* 连续错误锁定，倒计时 */
} login_substate_t;

/* ---- 错误锁定参数 ---- */
#define MAX_ERR_BEFORE_LOCK  3       /* 连续错误3次锁定 */
#define LOCK_FRAMES          1500    /* 锁定30秒(20ms/帧 * 1500 = 30s) */

/* ---- 键盘按钮(INPUT状态) ---- */
#define KB_BTN_W    45
#define KB_BTN_H    40
#define KB_GAP      6
#define KB_X0       115     /* (480 - 5*45 - 4*6) / 2 = 115 */
#define KB_Y0       90
#define KB_ROW2_X0  157     /* (480 - 3*45 - 2*6) / 2 = 157 */

typedef struct {
    int16_t x, y, w, h;
    const char *label;
} kb_btn_t;

/* 按钮索引: 0-9=数字, 10=C, 11=<-, 12=OK */
static const kb_btn_t kb_buttons[13] = {
    {KB_X0+0*51, KB_Y0+0*46, KB_BTN_W, KB_BTN_H, "1"},
    {KB_X0+1*51, KB_Y0+0*46, KB_BTN_W, KB_BTN_H, "2"},
    {KB_X0+2*51, KB_Y0+0*46, KB_BTN_W, KB_BTN_H, "3"},
    {KB_X0+3*51, KB_Y0+0*46, KB_BTN_W, KB_BTN_H, "4"},
    {KB_X0+4*51, KB_Y0+0*46, KB_BTN_W, KB_BTN_H, "5"},
    {KB_X0+0*51, KB_Y0+1*46, KB_BTN_W, KB_BTN_H, "6"},
    {KB_X0+1*51, KB_Y0+1*46, KB_BTN_W, KB_BTN_H, "7"},
    {KB_X0+2*51, KB_Y0+1*46, KB_BTN_W, KB_BTN_H, "8"},
    {KB_X0+3*51, KB_Y0+1*46, KB_BTN_W, KB_BTN_H, "9"},
    {KB_X0+4*51, KB_Y0+1*46, KB_BTN_W, KB_BTN_H, "0"},
    {KB_ROW2_X0+0*51, KB_Y0+2*46, KB_BTN_W, KB_BTN_H, "C"},
    {KB_ROW2_X0+1*51, KB_Y0+2*46, KB_BTN_W, KB_BTN_H, "<"},
    {KB_ROW2_X0+2*51, KB_Y0+2*46, KB_BTN_W, KB_BTN_H, "OK"},
};

/* ---- SELECT 状态的登录按钮 ---- */
#define LOGIN_BTN_W     120
#define LOGIN_BTN_H     50
#define LOGIN_BTN_X     ((SCR_W - LOGIN_BTN_W) / 2)   /* 180 */
#define LOGIN_BTN_Y     150

/* ---- PIN 密码框 ---- */
#define PIN_BOX_W   30
#define PIN_BOX_H   30
#define PIN_GAP     8
#define PIN_X0      ((SCR_W - 4*PIN_BOX_W - 3*PIN_GAP) / 2)  /* 168 */
#define PIN_Y0      35

#define STATUS_Y    72

/* ---- 系统状态栏（底部，与桌面一致） ---- */
#define SYSBAR_Y        295
#define CLR_SYSBAR      GREEN
static int last_sys_sec = -1;
static int last_sys_id  = -1;

/* ---- 模块状态 ---- */
static login_substate_t ls_state;
static char input_buf[PIN_LEN];
static int input_len;
static int16_t cursor_x, cursor_y;
static int16_t prev_cx, prev_cy;
static key_state_t prev_key;
static int err_frames;
static int need_redraw;
static int locked_mode;       /* 锁屏模式：1=锁屏唤醒(标题LOCKED) 0=首次登录 */
static int err_count;         /* 连续错误计数 */
static int lock_frames;       /* 锁定剩余帧数 */

/* 前向声明 */
static void draw_pin_box(void);
static void draw_status(void);
static void draw_sys_status_bar(int full);

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
/* 策略：先整块填背景色清除旧光标，再重绘所有与该区域相交的元素 */
static void restore_cursor_area(int16_t cx, int16_t cy)
{
    int16_t x1 = cx - CURSOR_R, x2 = cx + CURSOR_R;
    int16_t y1 = cy - CURSOR_R, y2 = cy + CURSOR_R;
    int i;

    /* 裁剪到屏幕内 */
    if (x1 < 0) x1 = 0;
    if (y1 < 0) y1 = 0;
    if (x2 >= SCR_W) x2 = SCR_W - 1;
    if (y2 >= SCR_H) y2 = SCR_H - 1;

    /* 第1步：整块填背景，彻底清除旧光标痕迹 */
    LCD_Fill(x1, y1, x2, y2, CLR_BG);

    /* 第2步：重绘所有与该区域相交的元素 */
    if (ls_state == LS_SELECT)
    {
        /* 标题文字 */
        if (rect_overlap(x1, y1, x2, y2, (SCR_W-144)/2, 80, (SCR_W-144)/2 + 143, 94))
            GUI_DrawString((SCR_W - 144) / 2, 80, "LOGIN SCREEN", CLR_TITLE, CLR_BG, 2);

        /* 登录按钮 */
        if (rect_overlap(x1, y1, x2, y2,
                         LOGIN_BTN_X, LOGIN_BTN_Y,
                         LOGIN_BTN_X + LOGIN_BTN_W - 1, LOGIN_BTN_Y + LOGIN_BTN_H - 1))
            GUI_DrawButton(LOGIN_BTN_X, LOGIN_BTN_Y, LOGIN_BTN_W, LOGIN_BTN_H,
                           "LOGIN", CLR_BTN_FG, CLR_BTN_BG, 2);
    }
    else if (ls_state == LS_INPUT)
    {
        /* 标题：锁屏模式 LOCKED(6字符)，登录模式 LOGIN(5字符) */
        if (locked_mode)
        {
            if (rect_overlap(x1, y1, x2, y2, 10, 10, 10 + 6*6*2 - 1, 10 + 7*2 - 1))
                GUI_DrawString(10, 10, "LOCKED", CLR_ERR, CLR_BG, 2);
        }
        else
        {
            if (rect_overlap(x1, y1, x2, y2, 10, 10, 10 + 5*6*2 - 1, 10 + 7*2 - 1))
                GUI_DrawString(10, 10, "LOGIN", CLR_TITLE, CLR_BG, 2);
        }

        /* 密码框 + 位数显示：直接重绘整个密码框区域(简单可靠) */
        if (rect_overlap(x1, y1, x2, y2,
                         PIN_X0 - 2, PIN_Y0 - 2,
                         PIN_X0 + PIN_LEN * (PIN_BOX_W + PIN_GAP) + 30,
                         PIN_Y0 + PIN_BOX_H + 2))
            draw_pin_box();

        /* 键盘按钮 */
        for (i = 0; i < 13; i++)
        {
            if (rect_overlap(x1, y1, x2, y2,
                             kb_buttons[i].x, kb_buttons[i].y,
                             kb_buttons[i].x + kb_buttons[i].w - 1,
                             kb_buttons[i].y + kb_buttons[i].h - 1))
                GUI_DrawButton(kb_buttons[i].x, kb_buttons[i].y,
                               kb_buttons[i].w, kb_buttons[i].h,
                               kb_buttons[i].label,
                               CLR_BTN_FG, CLR_BTN_BG, 2);
        }

        /* 状态提示：直接调用 draw_status 重绘(覆盖 ERR/INPUT/OK/LOCKED) */
        if (rect_overlap(x1, y1, x2, y2, 0, STATUS_Y, SCR_W - 1, STATUS_Y + 15))
            draw_status();

        /* 系统状态栏 */
        if (rect_overlap(x1, y1, x2, y2, 0, SYSBAR_Y, SCR_W - 1, SCR_H - 1))
            draw_sys_status_bar(1);
    }
}

/* ---- 画密码框 ---- */
static void draw_pin_box(void)
{
    int i;
    char buf[16];
    for (i = 0; i < PIN_LEN; i++)
    {
        uint16_t x = PIN_X0 + i * (PIN_BOX_W + PIN_GAP);
        GUI_DrawRect(x, PIN_Y0, PIN_BOX_W, PIN_BOX_H, CLR_PIN_BORDER);
        LCD_Fill(x + 1, PIN_Y0 + 1, x + PIN_BOX_W - 2, PIN_Y0 + PIN_BOX_H - 2, CLR_BG);
        if (i < input_len)
            GUI_DrawString(x + (PIN_BOX_W - 6) / 2, PIN_Y0 + (PIN_BOX_H - 14) / 2,
                           "*", CLR_PIN_DOT, CLR_BG, 2);
    }
    /* 右侧显示已输入位数 */
    snprintf(buf, sizeof(buf), "%d/%d", input_len, PIN_LEN);
    GUI_DrawString(PIN_X0 + PIN_LEN * (PIN_BOX_W + PIN_GAP) + 4, PIN_Y0 + (PIN_BOX_H - 7) / 2,
                   buf, CLR_HINT, CLR_BG, 1);
}

/* ---- 画键盘 ---- */
static void draw_keypad(void)
{
    int i;
    for (i = 0; i < 13; i++)
        GUI_DrawButton(kb_buttons[i].x, kb_buttons[i].y,
                       kb_buttons[i].w, kb_buttons[i].h,
                       kb_buttons[i].label,
                       CLR_BTN_FG, CLR_BTN_BG, 2);
}

/* ---- 画状态提示 ---- */
static void draw_status(void)
{
    char buf[32];
    LCD_Fill(0, STATUS_Y, SCR_W - 1, STATUS_Y + 16, CLR_BG);
    if (ls_state == LS_ERR)
    {
        /* 错误提示（短暂显示1秒） */
        uint16_t w = GUI_StringWidth("ERROR", 2);
        GUI_DrawString((SCR_W - w) / 2, STATUS_Y, "ERROR", CLR_ERR, CLR_BG, 2);
    }
    else if (ls_state == LS_INPUT)
    {
        /* 输入界面：持续显示密码提示（错1次以上才显示） */
        if (err_count >= 1)
        {
            snprintf(buf, sizeof(buf), "HINT: %c???", correct_pin[0]);
            uint16_t w = GUI_StringWidth(buf, 2);
            GUI_DrawString((SCR_W - w) / 2, STATUS_Y, buf, CLR_HINT, CLR_BG, 2);
        }
    }
    else if (ls_state == LS_OK)
    {
        uint16_t w = GUI_StringWidth("PASS OK", 2);
        GUI_DrawString((SCR_W - w) / 2, STATUS_Y, "PASS OK", CLR_OK, CLR_BG, 2);
    }
    else if (ls_state == LS_LOCKED)
    {
        int secs = lock_frames / 50 + 1;
        snprintf(buf, sizeof(buf), "LOCKED %ds", secs);
        uint16_t w = GUI_StringWidth(buf, 2);
        GUI_DrawString((SCR_W - w) / 2, STATUS_Y, buf, CLR_ERR, CLR_BG, 2);
    }
}

/* ---- 画系统状态栏（底部，与桌面一致） ---- */
/* full=1: 全量重绘 / full=0: 仅局部刷新时间区域 */
static void draw_sys_status_bar(int full)
{
    char buf[32];

    if (full)
    {
        LCD_Fill(0, SYSBAR_Y, SCR_W - 1, SCR_H - 1, CLR_BG);
        if (prev_key.id_connected)
            GUI_DrawString(280, SYSBAR_Y, "INPUT: OK", CLR_SYSBAR, CLR_BG, 1);
        else
            GUI_DrawString(280, SYSBAR_Y, "INPUT: NC", YELLOW, CLR_BG, 1);
    }

    /* 时间区域局部刷新 */
    LCD_Fill(120, SYSBAR_Y, 230, SCR_H - 1, CLR_BG);
    snprintf(buf, sizeof(buf), "TIME: %02d:%02d:%02d",
             RTC_GetHour(), RTC_GetMinute(), RTC_GetSecond());
    GUI_DrawString(120, SYSBAR_Y, buf, CLR_SYSBAR, CLR_BG, 1);
}

/* ---- 画 SELECT 界面 ---- */
static void draw_select_screen(void)
{
    LCD_Clear(CLR_BG);
    /* "LOGIN SCREEN" 12字符 scale=2, 宽=12*6*2=144 */
    GUI_DrawString((SCR_W - 144) / 2, 80, "LOGIN SCREEN", CLR_TITLE, CLR_BG, 2);
    GUI_DrawButton(LOGIN_BTN_X, LOGIN_BTN_Y, LOGIN_BTN_W, LOGIN_BTN_H,
                   "LOGIN", CLR_BTN_FG, CLR_BTN_BG, 2);
    draw_sys_status_bar(1);
}

/* ---- 画 INPUT 界面 ---- */
static void draw_input_screen(void)
{
    LCD_Clear(CLR_BG);
    /* 左上角标题：锁屏模式显示 LOCKED，登录模式显示 LOGIN */
    if (locked_mode)
        GUI_DrawString(10, 10, "LOCKED", CLR_ERR, CLR_BG, 2);
    else
        GUI_DrawString(10, 10, "LOGIN", CLR_TITLE, CLR_BG, 2);

    /* 密码框上方提示文字 */
    {
        const char *hint = locked_mode ? "Enter PIN to unlock" : "Enter PIN";
        uint16_t w = GUI_StringWidth(hint, 1);
        GUI_DrawString((SCR_W - w) / 2, PIN_Y0 - 14, hint, CLR_HINT, CLR_BG, 1);
    }

    /* 密码框 */
    draw_pin_box();
    /* 键盘 */
    draw_keypad();

    /* 底部操作提示（上移到 y=272 避免与系统状态栏 y=295 冲突） */
    GUI_DrawString(8, 272, "OK:Submit  <:Del  C:Clear  BACK:Back", CLR_HINT, CLR_BG, 1);

    /* 系统状态栏 */
    draw_sys_status_bar(1);
}

/* ---- 检测光标是否在某个键盘按钮上 ---- */
static int cursor_on_kb_btn(int *idx)
{
    int i;
    for (i = 0; i < 13; i++)
    {
        if (cursor_x >= kb_buttons[i].x && cursor_x < kb_buttons[i].x + kb_buttons[i].w &&
            cursor_y >= kb_buttons[i].y && cursor_y < kb_buttons[i].y + kb_buttons[i].h)
        {
            *idx = i;
            return 1;
        }
    }
    return 0;
}

/* ---- 输入数字 ---- */
static void input_digit(char d)
{
    if (input_len < PIN_LEN)
    {
        input_buf[input_len++] = d;
        draw_pin_box();
    }
}

/* ---- 验证密码 ---- */
static int verify_pin(void)
{
    if (input_len != PIN_LEN) return 0;
    return memcmp(input_buf, correct_pin, PIN_LEN) == 0;
}

/* ============================================================ */

void Login_Init(void)
{
    ls_state = LS_SELECT;
    input_len = 0;
    cursor_x = SCR_W / 2;
    cursor_y = LOGIN_BTN_Y + LOGIN_BTN_H / 2;
    memset(&prev_key, 0, sizeof(prev_key));
    err_frames = 0;
    err_count = 0;
    lock_frames = 0;
    locked_mode = 0;
    need_redraw = 1;
    last_sys_sec = -1;
    last_sys_id = -1;
}

void Login_LockInit(void)
{
    ls_state = LS_INPUT;
    input_len = 0;
    cursor_x = SCR_W / 2;
    cursor_y = KB_Y0 + KB_BTN_H / 2;
    memset(&prev_key, 0, sizeof(prev_key));
    err_frames = 0;
    err_count = 0;
    lock_frames = 0;
    locked_mode = 1;
    need_redraw = 1;
    last_sys_sec = -1;
    last_sys_id = -1;
}

int Login_IsPassed(void)
{
    return (ls_state == LS_OK);
}

int Login_Run(key_state_t *key)
{
    int moved = 0;

    /* 上升沿检测 */
    uint8_t e_up    = (!prev_key.up)    && key->up;
    uint8_t e_down  = (!prev_key.down)  && key->down;
    uint8_t e_left  = (!prev_key.left)  && key->left;
    uint8_t e_right = (!prev_key.right) && key->right;
    uint8_t e_ok    = (!prev_key.ok)    && key->ok;
    uint8_t e_back  = (!prev_key.back)  && key->back;

    /* 首次进入全屏重绘 */
    if (need_redraw)
    {
        /* 先更新 prev_key.id_connected，让 draw_select_screen/draw_input_screen
         * 内部的 draw_sys_status_bar(1) 能读到正确的 id_connected 值。
         * 只更新 id_connected，不影响 OK/back 等按键的边沿检测。 */
        prev_key.id_connected = key->id_connected;
        if (ls_state == LS_SELECT)
            draw_select_screen();
        else
            draw_input_screen();
        draw_cursor(cursor_x, cursor_y);
        need_redraw = 0;
        prev_cx = cursor_x;
        prev_cy = cursor_y;
        last_sys_sec = -1;
        last_sys_id = -1;
    }

    /* 系统状态栏定期刷新（秒变化或设备状态变化）
     * 注意：只更新 prev_key.id_connected（让 draw_sys_status_bar 读到正确值），
     * 不整体 prev_key = *key，避免 OK/back 边沿检测失效。 */
    {
        int cur_sec = RTC_GetSecond();
        int cur_id = key->id_connected;
        if (cur_id != last_sys_id)
        {
            prev_key.id_connected = cur_id;  /* 只更新 id_connected，不影响按键边沿 */
            draw_sys_status_bar(1);
            last_sys_sec = cur_sec;
            last_sys_id = cur_id;
        }
        else if (cur_sec != last_sys_sec)
        {
            draw_sys_status_bar(0);
            last_sys_sec = cur_sec;
        }
    }

    /* LOCKED 状态：连续错误锁定，倒计时后恢复输入 */
    if (ls_state == LS_LOCKED)
    {
        lock_frames--;
        if (lock_frames <= 0)
        {
            /* 锁定结束，回到输入界面 */
            ls_state = LS_INPUT;
            input_len = 0;
            need_redraw = 1;
        }
        else
        {
            /* 每秒刷新一次倒计时显示 */
            if ((lock_frames % 50) == 0)
                draw_status();
        }
        prev_key = *key;
        return 0;
    }

    /* ERR 状态：倒计时后恢复 */
    if (ls_state == LS_ERR)
    {
        err_frames--;
        if (err_frames <= 0)
        {
            ls_state = LS_INPUT;
            input_len = 0;
            draw_pin_box();
            draw_status();
            draw_cursor(cursor_x, cursor_y);
        }
        prev_key = *key;
        return 0;
    }

    /* OK 状态：已通过 */
    if (ls_state == LS_OK)
    {
        prev_key = *key;
        return 1;
    }

    /* ---- 光标移动 ---- */
    if (e_left)  { cursor_x -= CURSOR_STEP; moved = 1; }
    if (e_right) { cursor_x += CURSOR_STEP; moved = 1; }
    if (e_up)    { cursor_y -= CURSOR_STEP; moved = 1; }
    if (e_down)  { cursor_y += CURSOR_STEP; moved = 1; }

    if (moved)
    {
        /* 边界限制 */
        if (cursor_x < 0) cursor_x = 0;
        if (cursor_y < 0) cursor_y = 0;
        if (cursor_x >= SCR_W) cursor_x = SCR_W - 1;
        if (cursor_y >= SCR_H) cursor_y = SCR_H - 1;

        /* 恢复旧位置 + 画新位置 */
        restore_cursor_area(prev_cx, prev_cy);
        draw_cursor(cursor_x, cursor_y);
        prev_cx = cursor_x;
        prev_cy = cursor_y;
    }

    /* ---- OK 键处理 ---- */
    if (e_ok)
    {
        if (ls_state == LS_SELECT)
        {
            /* 检查光标是否在登录按钮上 */
            if (cursor_x >= LOGIN_BTN_X && cursor_x < LOGIN_BTN_X + LOGIN_BTN_W &&
                cursor_y >= LOGIN_BTN_Y && cursor_y < LOGIN_BTN_Y + LOGIN_BTN_H)
            {
                ls_state = LS_INPUT;
                input_len = 0;
                need_redraw = 1;
            }
        }
        else if (ls_state == LS_INPUT)
        {
            int idx;
            if (cursor_on_kb_btn(&idx))
            {
                if (idx <= 8)       input_digit('1' + idx);        /* 1-9 */
                else if (idx == 9)  input_digit('0');              /* 0 */
                else if (idx == 10) { input_len = 0; draw_pin_box(); } /* C */
                else if (idx == 11) { if (input_len > 0) { input_len--; draw_pin_box(); } } /* < */
                else /* idx == 12, OK */
                {
                    if (verify_pin())
                    {
                        ls_state = LS_OK;
                        err_count = 0;          /* 成功后清零错误计数 */
                        LogStore_LoginOK();
                        draw_status();
                        prev_key = *key;
                        return 1;
                    }
                    else
                    {
                        /* 密码错误：计数+1，达到上限进入锁定 */
                        err_count++;
                        LogStore_LoginFail();
                        if (err_count >= MAX_ERR_BEFORE_LOCK)
                        {
                            ls_state = LS_LOCKED;
                            lock_frames = LOCK_FRAMES;
                            err_count = 0;  /* 锁定结束后重新计数 */
                        }
                        else
                        {
                            ls_state = LS_ERR;
                            err_frames = 50;  /* 1秒提示 */
                        }
                        draw_status();
                    }
                }
            }
        }
    }

    /* ---- BACK 键：INPUT 状态下退格 ---- */
    if (e_back && ls_state == LS_INPUT && input_len > 0)
    {
        input_len--;
        draw_pin_box();
    }

    prev_key = *key;
    return 0;
}
