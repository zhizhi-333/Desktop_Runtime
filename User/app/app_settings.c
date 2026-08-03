#include "app_settings.h"
#include "app_manager.h"
#include "settings.h"
#include "gui.h"
#include "lcd.h"
#include "monitor.h"
#include "log_store.h"
#include <stddef.h>
#include <string.h>

/* ============================================================
 * 系统设置应用
 *
 * 5 项可调参数：
 *   1. 光标灵敏度（1-20）
 *   2. 光标大小（3-15）
 *   3. 屏幕亮度（0-100%）
 *   4. 系统音量（0-100%）
 *   5. 熄屏时间（5-60秒）
 *
 * 操作：
 *   ↑/↓   切换选中项
 *   ←/→   调整数值
 *   OK    保存到 Flash
 *   BACK  返回桌面（自动保存）
 * ============================================================ */

/* 颜色 */
#define CLR_BG          BLACK
#define CLR_TITLE       CYAN
#define CLR_LABEL       WHITE
#define CLR_VALUE       GREEN
#define CLR_SEL         YELLOW      /* 选中项高亮 */
#define CLR_HINT        GRAY
#define CLR_ERROR       RED

/* 布局 */
#define SCR_W   480
#define SCR_H   320
#define ROW_H   30                  /* 每行高度 */
#define COL1_X  30                  /* 标签列 */
#define COL2_X  280                 /* 数值列 */
#define START_Y 60                  /* 起始 Y */

/* 参数项数量 */
#define NUM_ITEMS   5
/* 按钮行 */
#define BTN_Y       (START_Y + NUM_ITEMS * ROW_H + 20)

/* ---- 模块状态 ---- */
static int need_redraw;
static int sel_item;            /* 当前选中项 0~4 */
static key_state_t prev_key;

/* 参数项信息 */
typedef struct {
    const char *label;
    uint32_t min_val;
    uint32_t max_val;
    const char *unit;
} item_info_t;

static const item_info_t items[NUM_ITEMS] = {
    { "Cursor Speed",  SENSITIVITY_MIN, SENSITIVITY_MAX, "px" },
    { "Cursor Size",   CURSOR_SIZE_MIN,  CURSOR_SIZE_MAX,  "px" },
    { "Brightness",    BRIGHTNESS_MIN,   BRIGHTNESS_MAX,   "%"  },
    { "Volume",        VOLUME_MIN,       VOLUME_MAX,       "%"  },
    { "Scrn Timeout",  TIMEOUT_MIN,      TIMEOUT_MAX,      "s"  },
};

/* 获取当前项的值 */
static uint32_t get_item_value(int idx)
{
    switch (idx)
    {
        case 0: return Settings_CursorSensitivity();
        case 1: return Settings_CursorSize();
        case 2: return Settings_Brightness();
        case 3: return Settings_Volume();
        case 4: return Settings_ScreenTimeout();
        default: return 0;
    }
}

/* 设置当前项的值 */
static void set_item_value(int idx, uint32_t v)
{
    switch (idx)
    {
        case 0: Settings_SetCursorSensitivity(v); break;
        case 1: Settings_SetCursorSize(v);        break;
        case 2: Settings_SetBrightness(v);        break;
        case 3: Settings_SetVolume(v);            break;
        case 4: Settings_SetScreenTimeout(v);     break;
    }
}

/* ---- 画一行 ---- */
static void draw_row(int idx, int selected)
{
    uint16_t y = START_Y + idx * ROW_H;
    uint16_t label_color = selected ? CLR_SEL : CLR_LABEL;
    uint16_t value_color = selected ? CLR_SEL : CLR_VALUE;
    char buf[16];
    uint32_t v = get_item_value(idx);

    /* 清除该行 */
    LCD_Fill(0, y, SCR_W - 1, y + ROW_H - 1, CLR_BG);

    /* 选中标记 */
    if (selected)
        GUI_DrawString(COL1_X - 15, y + 8, ">", CLR_SEL, CLR_BG, 1);

    /* 标签 */
    GUI_DrawString(COL1_X, y + 8, items[idx].label, label_color, CLR_BG, 1);

    /* 数值 + 单位 */
    GUI_DrawNum(COL2_X, y + 8, v, value_color, CLR_BG, 1);
    GUI_DrawString(COL2_X + 60, y + 8, items[idx].unit, label_color, CLR_BG, 1);

    /* 进度条（可视化） */
    {
        uint16_t bar_x = COL2_X + 90;
        uint16_t bar_y = y + 12;
        uint16_t bar_w = 80;
        uint16_t bar_h = 6;
        uint32_t range = items[idx].max_val - items[idx].min_val;
        uint32_t filled = (v - items[idx].min_val) * bar_w / range;

        /* 边框 */
        GUI_DrawRect(bar_x, bar_y, bar_w, bar_h, CLR_HINT);
        /* 填充 */
        if (filled > 0)
            LCD_Fill(bar_x + 1, bar_y + 1, bar_x + filled, bar_y + bar_h - 1, value_color);
    }
}

/* ---- 全屏重绘 ---- */
static void redraw_all(void)
{
    int i;

    LCD_Clear(CLR_BG);

    /* 标题 */
    GUI_DrawString(30, 15, "SETTINGS", CLR_TITLE, CLR_BG, 2);
    LCD_Fill(30, 42, SCR_W - 30, 43, CLR_TITLE);

    /* 各参数行 */
    for (i = 0; i < NUM_ITEMS; i++)
        draw_row(i, (i == sel_item));

    /* 按钮提示 */
    GUI_DrawString(30, BTN_Y, "[OK]Save  [BACK]Exit", CLR_HINT, CLR_BG, 1);

    /* 操作提示 */
    GUI_DrawString(30, BTN_Y + 20, "Up/Down: select  Left/Right: adjust", CLR_HINT, CLR_BG, 1);
}

/* ============================================================
 * 应用入口
 * ============================================================ */

void app_setting_create(void)
{
}

void app_setting_start(void)
{
    need_redraw = 1;
    sel_item = 0;
    memset(&prev_key, 0, sizeof(prev_key));
}

void app_setting_run(key_state_t *key)
{
    uint8_t e_up    = (!prev_key.up)    && key->up;
    uint8_t e_down  = (!prev_key.down)  && key->down;
    uint8_t e_left  = (!prev_key.left)  && key->left;
    uint8_t e_right = (!prev_key.right) && key->right;
    uint8_t e_ok    = (!prev_key.ok)    && key->ok;
    uint8_t e_back  = (!prev_key.back)  && key->back;

    /* BACK: 返回桌面 */
    if (e_back)
    {
        AppManager_GotoDesktop();
        prev_key = *key;
        return;
    }

    /* 输入事件计数 */
    if (e_up || e_down || e_left || e_right || e_ok)
        Monitor_IncInputEvent();

    /* 首次进入全屏重绘 */
    if (need_redraw)
    {
        redraw_all();
        need_redraw = 0;
        prev_key = *key;
        return;
    }

    /* 切换选中项 */
    if (e_up)
    {
        if (sel_item > 0) sel_item--;
        else sel_item = NUM_ITEMS - 1;
        draw_row(sel_item, 1);
        draw_row((sel_item == NUM_ITEMS - 1) ? 0 : sel_item + 1, 0);
    }
    if (e_down)
    {
        if (sel_item < NUM_ITEMS - 1) sel_item++;
        else sel_item = 0;
        draw_row(sel_item, 1);
        draw_row((sel_item == 0) ? NUM_ITEMS - 1 : sel_item - 1, 0);
    }

    /* 调整数值 */
    if (e_left || e_right)
    {
        uint32_t v = get_item_value(sel_item);
        uint32_t old_v = v;
        if (e_left)  v = (v > items[sel_item].min_val) ? v - 1 : items[sel_item].min_val;
        if (e_right) v = (v < items[sel_item].max_val) ? v + 1 : items[sel_item].max_val;
        set_item_value(sel_item, v);
        /* 值确实变化时记录日志 */
        if (v != old_v)
            LogStore_SettingChange(items[sel_item].label, v);
        draw_row(sel_item, 1);
    }

    /* OK 键：保存到 Flash */
    if (e_ok)
    {
        if (Settings_Save() == 0)
        {
            /* 显示保存成功提示 */
            LCD_Fill(30, BTN_Y, SCR_W - 30, BTN_Y + 16, CLR_BG);
            GUI_DrawString(30, BTN_Y, "SAVED!", CLR_VALUE, CLR_BG, 1);
        }
        else
        {
            LCD_Fill(30, BTN_Y, SCR_W - 30, BTN_Y + 16, CLR_BG);
            GUI_DrawString(30, BTN_Y, "SAVE FAILED!", CLR_ERROR, CLR_BG, 1);
            Monitor_IncError();
        }
    }

    prev_key = *key;
}

void app_setting_pause(void)
{
    /* 离开时自动保存 */
    Settings_Save();
    need_redraw = 1;
}
