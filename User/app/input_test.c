#include "input_test.h"
#include "lcd.h"
#include "key.h"
#include "encoder.h"

#define ICON_W          60
#define ICON_H          40
#define CURSOR_R        6
#define STEP            2

#define ICON_COLOR_A    0x001F
#define ICON_COLOR_B    0xF800
#define CURSOR_COLOR    0x0000

static const uint16_t bg_colors[] = {
    0xFFFF, 0xF800, 0x07E0, 0x001F, 0xFFE0
};
#define BG_COLOR_NUM    (sizeof(bg_colors) / sizeof(bg_colors[0]))

static int16_t cursor_x, cursor_y;
static int16_t icon_x, icon_y;
static uint8_t icon_color_idx;
static uint8_t bg_idx;
static uint8_t ok_held;
static uint8_t dragging;
static uint8_t first_run;

static void clamp_icon(void)
{
    if (icon_x < 0)                  icon_x = 0;
    if (icon_y < 0)                  icon_y = 0;
    if (icon_x + ICON_W > g_lcd.width)  icon_x = (int16_t)g_lcd.width - ICON_W;
    if (icon_y + ICON_H > g_lcd.height) icon_y = (int16_t)g_lcd.height - ICON_H;
}

static void clamp_cursor(void)
{
    if (cursor_x < 0)                  cursor_x = 0;
    if (cursor_y < 0)                  cursor_y = 0;
    if (cursor_x >= g_lcd.width)       cursor_x = (int16_t)g_lcd.width - 1;
    if (cursor_y >= g_lcd.height)      cursor_y = (int16_t)g_lcd.height - 1;
}

static int is_on_icon(int16_t x, int16_t y)
{
    return x >= icon_x && x < icon_x + ICON_W
        && y >= icon_y && y < icon_y + ICON_H;
}

static void draw_app_icon(void)
{
    uint16_t c = icon_color_idx ? ICON_COLOR_B : ICON_COLOR_A;
    LCD_Fill((uint16_t)(icon_x - 1), (uint16_t)(icon_y - 1),
             (uint16_t)(icon_x + ICON_W), (uint16_t)(icon_y + ICON_H), 0x0000);
    LCD_Fill((uint16_t)icon_x, (uint16_t)icon_y,
             (uint16_t)(icon_x + ICON_W - 1), (uint16_t)(icon_y + ICON_H - 1), c);
}

static void draw_cursor(int16_t cx, int16_t cy)
{
    if (cx >= 0 && cx < g_lcd.width && cy >= 0 && cy < g_lcd.height)
    {
        int16_t x1 = cx - CURSOR_R; if (x1 < 0) x1 = 0;
        int16_t x2 = cx + CURSOR_R; if (x2 >= g_lcd.width) x2 = g_lcd.width - 1;
        int16_t y1 = cy - CURSOR_R; if (y1 < 0) y1 = 0;
        int16_t y2 = cy + CURSOR_R; if (y2 >= g_lcd.height) y2 = g_lcd.height - 1;
        LCD_Fill((uint16_t)x1, (uint16_t)cy, (uint16_t)x2, (uint16_t)cy, CURSOR_COLOR);
        LCD_Fill((uint16_t)cx, (uint16_t)y1, (uint16_t)cx, (uint16_t)y2, CURSOR_COLOR);
    }
}

static void restore_cursor_area(int16_t cx, int16_t cy)
{
    int16_t x1 = cx - CURSOR_R; if (x1 < 0) x1 = 0;
    int16_t x2 = cx + CURSOR_R; if (x2 >= g_lcd.width) x2 = g_lcd.width - 1;
    int16_t y1 = cy - CURSOR_R; if (y1 < 0) y1 = 0;
    int16_t y2 = cy + CURSOR_R; if (y2 >= g_lcd.height) y2 = g_lcd.height - 1;
    LCD_Fill((uint16_t)x1, (uint16_t)y1, (uint16_t)x2, (uint16_t)y2, bg_colors[bg_idx]);
    draw_app_icon();
}

void InputTest_Init(void)
{
    Key_Init();
    Encoder_Init();

    cursor_x = 20;
    cursor_y = 20;
    icon_x = ((int16_t)g_lcd.width - ICON_W) / 2;
    icon_y = ((int16_t)g_lcd.height - ICON_H) / 2;
    icon_color_idx = 0;
    bg_idx = 0;
    ok_held = 0;
    dragging = 0;
    first_run = 1;
}

void InputTest_Run(void)
{
    key_state_t key;
    int16_t enc_delta;

    Key_Scan(&key);
    enc_delta = Encoder_GetDelta();

    int16_t new_bg = (int16_t)bg_idx;
    if (enc_delta > 0 && bg_idx < BG_COLOR_NUM - 1) new_bg = bg_idx + 1;
    else if (enc_delta < 0 && bg_idx > 0)           new_bg = bg_idx - 1;

    if ((uint8_t)new_bg != bg_idx)
    {
        bg_idx = (uint8_t)new_bg;
        LCD_Clear(bg_colors[bg_idx]);
        draw_app_icon();
        draw_cursor(cursor_x, cursor_y);
        first_run = 0;
        return;
    }

    if (first_run)
    {
        LCD_Clear(bg_colors[bg_idx]);
        draw_app_icon();
        draw_cursor(cursor_x, cursor_y);
        first_run = 0;
        return;
    }
    int16_t dx = 0, dy = 0;
    if (key.left)   dx -= STEP;
    if (key.right)  dx += STEP;
    if (key.up)     dy -= STEP;
    if (key.down)   dy += STEP;

    if (dx || dy)
    {
        if (ok_held && is_on_icon(cursor_x, cursor_y))
        {
            dragging = 1;
            icon_x += dx;
            icon_y += dy;
            clamp_icon();
            draw_app_icon();
            draw_cursor(cursor_x, cursor_y);
        }
        else if (!ok_held)
        {
            restore_cursor_area(cursor_x, cursor_y);
            cursor_x += dx;
            cursor_y += dy;
            clamp_cursor();
            draw_cursor(cursor_x, cursor_y);
        }
    }

    if (key.ok)
    {
        if (!ok_held)
        {
            ok_held = 1;
            dragging = 0;
        }
    }
    else
    {
        if (ok_held)
        {
            if (!dragging && is_on_icon(cursor_x, cursor_y))
            {
                icon_color_idx = !icon_color_idx;
                restore_cursor_area(cursor_x, cursor_y);
                draw_cursor(cursor_x, cursor_y);
            }
            ok_held = 0;
            dragging = 0;
        }
    }

    if (key.back && icon_color_idx != 0)
    {
        icon_color_idx = 0;
        restore_cursor_area(cursor_x, cursor_y);
        draw_cursor(cursor_x, cursor_y);
    }
}
