#ifndef GUI_H
#define GUI_H

#include "lcd.h"

/* 5x7 点阵字体，放大显示用 */
/* 字符宽度(像素) = 5 * scale，高度 = 7 * scale，字符间距 = 1 * scale */
#define GUI_FONT_W      5
#define GUI_FONT_H      7

/* 默认颜色 */
#define GUI_FG_DEFAULT  WHITE
#define GUI_BG_DEFAULT  BLACK

/* 在指定位置画一个字符(前景色/背景色)
 * x,y    左上角坐标
 * c      ASCII 字符(支持 0x20~0x7E)
 * fg     前景色
 * bg     背景色(传 -1 表示透明，不画背景)
 * scale  放大倍数(1=原始5x7, 2=10x14, ...)
 */
void GUI_DrawChar(uint16_t x, uint16_t y, char c,
                  uint16_t fg, int16_t bg, uint8_t scale);

/* 画字符串(自动换行不处理，调用者负责不越界) */
void GUI_DrawString(uint16_t x, uint16_t y, const char *str,
                    uint16_t fg, int16_t bg, uint8_t scale);

/* 画数字(无符号32位) */
void GUI_DrawNum(uint16_t x, uint16_t y, uint32_t num,
                 uint16_t fg, int16_t bg, uint8_t scale);

/* 画一个矩形框(空心) */
void GUI_DrawRect(uint16_t x, uint16_t y, uint16_t w, uint16_t h, uint16_t color);

/* 画一个填充按钮：背景 + 居中文字 */
void GUI_DrawButton(uint16_t x, uint16_t y, uint16_t w, uint16_t h,
                    const char *label, uint16_t fg, uint16_t bg, uint8_t scale);

/* 获取字符串像素宽度(含字间距) */
uint16_t GUI_StringWidth(const char *str, uint8_t scale);

#endif /* GUI_H */
