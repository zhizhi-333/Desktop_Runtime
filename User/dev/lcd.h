#ifndef LCD_DRIVER_H
#define LCD_DRIVER_H

#include "main.h"

#define USE_HORIZONTAL  1

#define LCD_W   320
#define LCD_H   480

#define WHITE       0xFFFF
#define BLACK       0x0000
#define RED         0xF800
#define GREEN       0x07E0
#define BLUE        0x001F
#define YELLOW      0xFFE0
#define CYAN        0x7FFF
#define MAGENTA     0xF81F
#define GRAY        0x8430
#define BRRED       0xFC07
#define GBLUE       0x07FF
#define BROWN       0xBC40

#define LCD_CS_Pin      GPIO_PIN_12
#define LCD_CS_Port     GPIOB
#define LCD_DC_Pin      GPIO_PIN_13
#define LCD_DC_Port     GPIOB
#define LCD_RST_Pin     GPIO_PIN_14
#define LCD_RST_Port    GPIOB
#define LCD_LED_Pin     GPIO_PIN_15
#define LCD_LED_Port    GPIOB

#define LCD_CS_L()      HAL_GPIO_WritePin(LCD_CS_Port, LCD_CS_Pin, GPIO_PIN_RESET)
#define LCD_CS_H()      HAL_GPIO_WritePin(LCD_CS_Port, LCD_CS_Pin, GPIO_PIN_SET)
#define LCD_DC_L()      HAL_GPIO_WritePin(LCD_DC_Port, LCD_DC_Pin, GPIO_PIN_RESET)
#define LCD_DC_H()      HAL_GPIO_WritePin(LCD_DC_Port, LCD_DC_Pin, GPIO_PIN_SET)
#define LCD_RST_L()     HAL_GPIO_WritePin(LCD_RST_Port, LCD_RST_Pin, GPIO_PIN_RESET)
#define LCD_RST_H()     HAL_GPIO_WritePin(LCD_RST_Port, LCD_RST_Pin, GPIO_PIN_SET)
#define LCD_LED_ON()    HAL_GPIO_WritePin(LCD_LED_Port, LCD_LED_Pin, GPIO_PIN_SET)
#define LCD_LED_OFF()   HAL_GPIO_WritePin(LCD_LED_Port, LCD_LED_Pin, GPIO_PIN_RESET)

typedef struct
{
    uint16_t width;
    uint16_t height;
    uint16_t id;
    uint8_t  dir;
    uint16_t wramcmd;
    uint16_t setxcmd;
    uint16_t setycmd;
} lcd_dev_t;

extern lcd_dev_t g_lcd;
extern uint16_t g_point_color;
extern uint16_t g_back_color;

void LCD_Init(void);
void LCD_Clear(uint16_t color);
void LCD_SetWindows(uint16_t x1, uint16_t y1, uint16_t x2, uint16_t y2);
void LCD_SetCursor(uint16_t x, uint16_t y);
void LCD_DrawPoint(uint16_t x, uint16_t y);
void LCD_DrawLine(uint16_t x1, uint16_t y1, uint16_t x2, uint16_t y2);
void LCD_Fill(uint16_t x1, uint16_t y1, uint16_t x2, uint16_t y2, uint16_t color);
void LCD_ShowColorBar(void);

#endif
