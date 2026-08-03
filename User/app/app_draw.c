#include "app_draw.h"
#include "app_manager.h"
#include "gui.h"
#include "lcd.h"
#include "monitor.h"
#include "settings.h"
#include "encoder.h"
#include <stddef.h>

/* ============================================================
 * 画图应用
 *
 * 界面布局 (480x320 横屏):
 *   ┌──────────────────────────────────┐
 *   │ DRAW  DN  RED  X:240 Y:180  CLR │  y=0~28 状态栏
 *   ├──────────────────────────────────┤
 *   │                                  │
 *   │            画布区域               │  y=28~320
 *   │          （落笔移动画线）         │
 *   │                                  │
 *   └──────────────────────────────────┘
 *
 * 操作:
 *   方向键    移动画笔（步长从设置读取）
 *   OK        切换落笔/抬笔
 *   编码器旋  切换画笔颜色（7 色循环）
 *   编码器按  清空画布
 *   BACK      返回桌面
 *
 * 说明:
 *   - 落笔时移动会画线（从前一位置到当前位置）
 *   - 抬笔时移动不画线，只改变画笔位置
 *   - 画笔位置在状态栏用坐标显示（画布上不显示光标，避免污染画布）
 *   - 切换应用再回来时画布内容会丢失（LCD 被其他应用覆盖），后续可加 SD 卡保存
 * ============================================================ */

#define SCR_W       480
#define SCR_H       320
#define STATUS_H    28
#define CANVAS_Y0   STATUS_H          /* 画布起始 Y */

#define PEN_STEP    Settings_CursorSensitivity()

/* ---- 颜色调色板 ---- */
static const uint16_t palette[] = {
    WHITE, RED, GREEN, BLUE, YELLOW, CYAN, MAGENTA
};
static const char *color_names[] = {
    "WHT", "RED", "GRN", "BLU", "YLW", "CYN", "MGT"
};
#define PALETTE_COUNT   (sizeof(palette) / sizeof(palette[0]))

/* ---- 模块状态 ---- */
static int need_redraw;
static int pen_down;                /* 0=抬笔, 1=落笔 */
static int color_idx;               /* 当前颜色索引 */
static int16_t pen_x, pen_y;        /* 画笔当前位置 */
static int16_t prev_pen_x, prev_pen_y;  /* 上次位置（画线起点） */
static key_state_t prev_key;

/* ---- 画状态栏 ---- */
static void draw_status(void)
{
    LCD_Fill(0, 0, SCR_W - 1, STATUS_H - 1, BLACK);

    /* 标题 */
    GUI_DrawString(5, 8, "DRAW", CYAN, BLACK, 1);

    /* 画笔状态 DN/UP */
    GUI_DrawString(50, 8, pen_down ? "DN" : "UP",
                   pen_down ? GREEN : GRAY, BLACK, 1);

    /* 当前颜色名（用画笔颜色显示） */
    GUI_DrawString(80, 8, color_names[color_idx],
                   palette[color_idx], BLACK, 1);

    /* 画笔坐标 */
    GUI_DrawString(120, 8, "X:", WHITE, BLACK, 1);
    GUI_DrawNum(135, 8, pen_x, WHITE, BLACK, 1);
    GUI_DrawString(175, 8, "Y:", WHITE, BLACK, 1);
    GUI_DrawNum(190, 8, pen_y, WHITE, BLACK, 1);

    /* 清空提示 */
    GUI_DrawString(SCR_W - 55, 8, "EC=CLR", GRAY, BLACK, 1);
}

/* ---- 全屏重绘 ---- */
static void redraw_all(void)
{
    LCD_Clear(BLACK);
    draw_status();
    /* 画布区域保持黑色 */
}

/* ============================================================
 * 应用入口
 * ============================================================ */

void app_draw_create(void)
{
    /* 首次创建：画笔默认在画布中央 */
    pen_x = SCR_W / 2;
    pen_y = (CANVAS_Y0 + SCR_H) / 2;
}

void app_draw_start(void)
{
    /* 从暂停恢复：需要重绘 */
    need_redraw = 1;
}

void app_draw_run(key_state_t *key)
{
    uint8_t e_up    = (!prev_key.up)    && key->up;
    uint8_t e_down  = (!prev_key.down)  && key->down;
    uint8_t e_left  = (!prev_key.left)  && key->left;
    uint8_t e_right = (!prev_key.right) && key->right;
    uint8_t e_ok    = (!prev_key.ok)    && key->ok;
    uint8_t e_back  = (!prev_key.back)  && key->back;
    uint8_t e_ecsw  = (!prev_key.ec_sw) && key->ec_sw;
    int16_t enc_delta = Encoder_GetDelta();
    int16_t moved = 0;

    /* BACK: 返回桌面 */
    if (e_back)
    {
        AppManager_GotoDesktop();
        prev_key = *key;
        return;
    }
    int status_changed = 0;

    /* 输入事件计数 */
    if (e_up || e_down || e_left || e_right || e_ok || e_ecsw)
        Monitor_IncInputEvent();

    /* 首次进入全屏重绘 */
    if (need_redraw)
    {
        redraw_all();
        need_redraw = 0;
        prev_key = *key;
        return;
    }

    /* ---- 画笔移动 ---- */
    prev_pen_x = pen_x;
    prev_pen_y = pen_y;

    if (e_left)  { pen_x -= PEN_STEP; moved = 1; }
    if (e_right) { pen_x += PEN_STEP; moved = 1; }
    if (e_up)    { pen_y -= PEN_STEP; moved = 1; }
    if (e_down)  { pen_y += PEN_STEP; moved = 1; }

    if (moved)
    {
        /* 边界限制：画笔只在画布区域内 */
        if (pen_x < 0) pen_x = 0;
        if (pen_y < CANVAS_Y0) pen_y = CANVAS_Y0;
        if (pen_x >= SCR_W) pen_x = SCR_W - 1;
        if (pen_y >= SCR_H) pen_y = SCR_H - 1;

        /* 落笔时画线（从前一位置到当前位置） */
        if (pen_down)
        {
            g_point_color = palette[color_idx];
            LCD_DrawLine(prev_pen_x, prev_pen_y, pen_x, pen_y);
        }

        status_changed = 1;  /* 坐标变了，更新状态栏 */
    }

    /* ---- OK 键：切换落笔/抬笔 ---- */
    if (e_ok)
    {
        pen_down = !pen_down;
        status_changed = 1;
    }

    /* ---- 编码器旋转：切换颜色 ---- */
    if (enc_delta > 0)
    {
        color_idx = (color_idx + 1) % PALETTE_COUNT;
        status_changed = 1;
    }
    else if (enc_delta < 0)
    {
        color_idx = (color_idx - 1 + PALETTE_COUNT) % PALETTE_COUNT;
        status_changed = 1;
    }

    /* ---- 编码器按压：清空画布 ---- */
    if (e_ecsw)
    {
        LCD_Fill(0, CANVAS_Y0, SCR_W - 1, SCR_H - 1, BLACK);
    }

    /* ---- 状态栏更新 ---- */
    if (status_changed)
        draw_status();

    prev_key = *key;
}

void app_draw_pause(void)
{
    /* 被切换走：下次回来需要重绘（画布内容会丢失） */
    need_redraw = 1;
}
