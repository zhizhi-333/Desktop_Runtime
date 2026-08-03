#include "app_sysmonitor.h"
#include "app_manager.h"
#include "monitor.h"
#include "gui.h"
#include "lcd.h"
#include "key.h"
#include <stddef.h>

/* ============================================================
 * 系统监控应用
 *
 * 显示内容：
 *   - 运行时间（HH:MM:SS）
 *   - 当前系统状态（LOGIN/DESKTOP/APP）
 *   - 剩余堆 / 历史最小堆
 *   - 任务栈水位（Monitor/LED/Input）
 *   - 事件计数（输入/丢弃/错误）
 *
 * 刷新策略：
 *   - 进入时全屏重绘
 *   - 运行中每 500ms 局部刷新数据（避免闪烁）
 *   - 按 BACK 返回桌面（由 AppManager 处理）
 * ============================================================ */

/* 颜色定义 */
#define CLR_BG          BLACK
#define CLR_TITLE       CYAN
#define CLR_LABEL       WHITE
#define CLR_VALUE       GREEN
#define CLR_WARN        YELLOW
#define CLR_ERROR       RED

/* 布局 */
#define SCR_W   480
#define SCR_H   320
#define ROW_H   18      /* 每行高度(调小以容纳更多行) */
#define COL1_X  20      /* 标签列 */
#define COL2_X  180     /* 数值列 */
#define START_Y 48      /* 起始 Y */

/* ---- 模块状态 ---- */
static int need_redraw;
static uint32_t last_refresh;   /* 上次刷新的 tick */
static monitor_data_t last_data; /* 上次显示的数据（用于对比变化） */
static key_state_t prev_key;

/* ---- 系统状态字符串 ---- */
static const char *state_str(sys_runtime_state_t s)
{
    switch (s)
    {
        case SYS_STATE_BOOT:    return "BOOT";
        case SYS_STATE_LOGIN:   return "LOGIN";
        case SYS_STATE_DESKTOP: return "DESKTOP";
        case SYS_STATE_APP:     return "APP";
        default:                return "???";
    }
}

/* ---- 格式化运行时间 HH:MM:SS ---- */
static void format_uptime(uint32_t sec, char *buf)
{
    uint32_t h = sec / 3600;
    uint32_t m = (sec % 3600) / 60;
    uint32_t s = sec % 60;
    /* 简单格式：HH:MM:SS，不做补零的复杂处理 */
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

/* ---- 画一行：标签 + 数值 ---- */
static void draw_row(uint16_t y, const char *label, const char *value, uint16_t vcolor)
{
    /* 清除该行 */
    LCD_Fill(0, y, SCR_W - 1, y + ROW_H - 1, CLR_BG);
    GUI_DrawString(COL1_X, y + 4, label, CLR_LABEL, CLR_BG, 1);
    GUI_DrawString(COL2_X, y + 4, value, vcolor, CLR_BG, 1);
}

/* ---- 画标题 ---- */
static void draw_title(void)
{
    GUI_DrawString(20, 12, "SYSTEM MONITOR", CLR_TITLE, CLR_BG, 2);
    /* 分隔线 */
    LCD_Fill(20, 38, SCR_W - 20, 39, CLR_TITLE);
}

/* ---- 辅助: 画一行 标签 + 数字 + 单位 ---- */
static void draw_row_num(uint16_t y, const char *label, uint32_t value,
                         uint16_t vcolor, const char *unit)
{
    LCD_Fill(0, y, SCR_W - 1, y + ROW_H - 1, CLR_BG);
    GUI_DrawString(COL1_X, y + 3, label, CLR_LABEL, CLR_BG, 1);
    GUI_DrawNum(COL2_X, y + 3, value, vcolor, CLR_BG, 1);
    if (unit)
        GUI_DrawString(COL2_X + 80, y + 3, unit, CLR_LABEL, CLR_BG, 1);
}

/* ---- 全屏重绘 ---- */
static void redraw_all(const monitor_data_t *d)
{
    char buf[32];

    LCD_Clear(CLR_BG);
    draw_title();

    /* 运行时间 */
    format_uptime(d->uptime_sec, buf);
    draw_row(START_Y + 0 * ROW_H, "Uptime:", buf, CLR_VALUE);

    /* 系统状态 */
    draw_row(START_Y + 1 * ROW_H, "State:", state_str(d->state), CLR_VALUE);

    /* 剩余堆 */
    draw_row_num(START_Y + 2 * ROW_H, "FreeHeap:", (uint32_t)d->free_heap, CLR_VALUE, "B");

    /* 最小堆 */
    draw_row_num(START_Y + 3 * ROW_H, "MinHeap:", (uint32_t)d->min_heap, CLR_WARN, "B");

    /* 栈水位 - Monitor */
    draw_row_num(START_Y + 4 * ROW_H, "MonStack:", d->monitor_stack, CLR_VALUE, "W");

    /* 栈水位 - LED */
    draw_row_num(START_Y + 5 * ROW_H, "LedStack:", d->led_stack, CLR_VALUE, "W");

    /* 栈水位 - Input */
    draw_row_num(START_Y + 6 * ROW_H, "InpStack:", d->input_stack, CLR_VALUE, "W");

    /* 栈水位 - FileTask */
    draw_row_num(START_Y + 7 * ROW_H, "FileStack:", d->file_stack, CLR_VALUE, "W");

    /* 栈水位 - MusicTask */
    draw_row_num(START_Y + 8 * ROW_H, "MusicStack:", d->music_stack, CLR_VALUE, "W");

    /* 队列水位 - 文件请求 */
    draw_row_num(START_Y + 9 * ROW_H, "FileReqQ:", d->file_req_qwm, CLR_VALUE, "");

    /* 队列水位 - 文件响应 */
    draw_row_num(START_Y + 10 * ROW_H, "FileRespQ:", d->file_resp_qwm, CLR_VALUE, "");

    /* 队列水位 - 音乐命令 */
    draw_row_num(START_Y + 11 * ROW_H, "MusicCmdQ:", d->music_cmd_qwm, CLR_VALUE, "");

    /* 输入事件 */
    draw_row_num(START_Y + 12 * ROW_H, "InpEvents:", d->input_events, CLR_VALUE, "");

    /* 丢弃事件 */
    draw_row_num(START_Y + 13 * ROW_H, "DrpEvents:", d->dropped_events, CLR_WARN, "");

    /* 错误计数 */
    draw_row_num(START_Y + 14 * ROW_H, "Errors:", d->error_count,
                 (d->error_count > 0) ? CLR_ERROR : CLR_VALUE, "");

    /* 底部提示 */
    GUI_DrawString(20, SCR_H - 16, "[BACK] to desktop", CLR_LABEL, CLR_BG, 1);
}

/* ---- 局部刷新（只更新变化的行）---- */
static void refresh_data(const monitor_data_t *d)
{
    char buf[32];
    uint16_t y;

    /* 运行时间（总会变） */
    format_uptime(d->uptime_sec, buf);
    y = START_Y + 0 * ROW_H;
    LCD_Fill(COL2_X, y, SCR_W - 1, y + ROW_H - 1, CLR_BG);
    GUI_DrawString(COL2_X, y + 3, buf, CLR_VALUE, CLR_BG, 1);

    /* 系统状态（可能变化） */
    if (d->state != last_data.state)
    {
        y = START_Y + 1 * ROW_H;
        LCD_Fill(COL2_X, y, SCR_W - 1, y + ROW_H - 1, CLR_BG);
        GUI_DrawString(COL2_X, y + 3, state_str(d->state), CLR_VALUE, CLR_BG, 1);
    }

    /* 剩余堆（可能变化） */
    if (d->free_heap != last_data.free_heap)
    {
        y = START_Y + 2 * ROW_H;
        LCD_Fill(COL2_X, y, SCR_W - 1, y + ROW_H - 1, CLR_BG);
        GUI_DrawNum(COL2_X, y + 3, (uint32_t)d->free_heap, CLR_VALUE, CLR_BG, 1);
    }

    /* 最小堆（可能变化） */
    if (d->min_heap != last_data.min_heap)
    {
        y = START_Y + 3 * ROW_H;
        LCD_Fill(COL2_X, y, SCR_W - 1, y + ROW_H - 1, CLR_BG);
        GUI_DrawNum(COL2_X, y + 3, (uint32_t)d->min_heap, CLR_WARN, CLR_BG, 1);
    }

    /* 栈水位（可能变化） */
    if (d->monitor_stack != last_data.monitor_stack)
    {
        y = START_Y + 4 * ROW_H;
        LCD_Fill(COL2_X, y, SCR_W - 1, y + ROW_H - 1, CLR_BG);
        GUI_DrawNum(COL2_X, y + 3, d->monitor_stack, CLR_VALUE, CLR_BG, 1);
    }
    if (d->led_stack != last_data.led_stack)
    {
        y = START_Y + 5 * ROW_H;
        LCD_Fill(COL2_X, y, SCR_W - 1, y + ROW_H - 1, CLR_BG);
        GUI_DrawNum(COL2_X, y + 3, d->led_stack, CLR_VALUE, CLR_BG, 1);
    }
    if (d->input_stack != last_data.input_stack)
    {
        y = START_Y + 6 * ROW_H;
        LCD_Fill(COL2_X, y, SCR_W - 1, y + ROW_H - 1, CLR_BG);
        GUI_DrawNum(COL2_X, y + 3, d->input_stack, CLR_VALUE, CLR_BG, 1);
    }

    /* 队列水位（可能变化） */
    if (d->file_req_qwm != last_data.file_req_qwm)
    {
        y = START_Y + 9 * ROW_H;
        LCD_Fill(COL2_X, y, SCR_W - 1, y + ROW_H - 1, CLR_BG);
        GUI_DrawNum(COL2_X, y + 3, d->file_req_qwm, CLR_VALUE, CLR_BG, 1);
    }
    if (d->file_resp_qwm != last_data.file_resp_qwm)
    {
        y = START_Y + 10 * ROW_H;
        LCD_Fill(COL2_X, y, SCR_W - 1, y + ROW_H - 1, CLR_BG);
        GUI_DrawNum(COL2_X, y + 3, d->file_resp_qwm, CLR_VALUE, CLR_BG, 1);
    }
    if (d->music_cmd_qwm != last_data.music_cmd_qwm)
    {
        y = START_Y + 11 * ROW_H;
        LCD_Fill(COL2_X, y, SCR_W - 1, y + ROW_H - 1, CLR_BG);
        GUI_DrawNum(COL2_X, y + 3, d->music_cmd_qwm, CLR_VALUE, CLR_BG, 1);
    }

    /* 事件计数（可能变化） */
    if (d->input_events != last_data.input_events)
    {
        y = START_Y + 12 * ROW_H;
        LCD_Fill(COL2_X, y, SCR_W - 1, y + ROW_H - 1, CLR_BG);
        GUI_DrawNum(COL2_X, y + 3, d->input_events, CLR_VALUE, CLR_BG, 1);
    }
    if (d->dropped_events != last_data.dropped_events)
    {
        y = START_Y + 13 * ROW_H;
        LCD_Fill(COL2_X, y, SCR_W - 1, y + ROW_H - 1, CLR_BG);
        GUI_DrawNum(COL2_X, y + 3, d->dropped_events, CLR_WARN, CLR_BG, 1);
    }
    if (d->error_count != last_data.error_count)
    {
        y = START_Y + 14 * ROW_H;
        LCD_Fill(COL2_X, y, SCR_W - 1, y + ROW_H - 1, CLR_BG);
        GUI_DrawNum(COL2_X, y + 3, d->error_count,
                    (d->error_count > 0) ? CLR_ERROR : CLR_VALUE, CLR_BG, 1);
    }
}

/* ============================================================
 * 应用入口函数（供注册表调用）
 * ============================================================ */

void app_sysmonitor_create(void)
{
    /* 首次创建：无特殊资源需要分配 */
}

void app_sysmonitor_start(void)
{
    /* 从暂停恢复：需要全屏重绘 */
    need_redraw = 1;
}

void app_sysmonitor_run(key_state_t *key)
{
    monitor_data_t data;
    uint32_t now;
    uint8_t e_back = (!prev_key.back) && key->back;

    /* BACK: 返回桌面 */
    if (e_back)
    {
        AppManager_GotoDesktop();
        prev_key = *key;
        return;
    }

    Monitor_GetData(&data);
    now = data.tick;

    /* 首次进入或从暂停恢复：全屏重绘 */
    if (need_redraw)
    {
        redraw_all(&data);
        last_data = data;
        last_refresh = now;
        need_redraw = 0;
        prev_key = *key;
        return;
    }

    /* 每 500ms 刷新一次数据 */
    if ((now - last_refresh) >= 500)
    {
        refresh_data(&data);
        last_data = data;
        last_refresh = now;
    }

    prev_key = *key;
}

void app_sysmonitor_pause(void)
{
    /* 被切换走：下次回来时需要重绘 */
    need_redraw = 1;
}
