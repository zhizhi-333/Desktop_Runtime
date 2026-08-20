#include "lcd.h"
#include "stm32f4xx_hal.h"

static SPI_HandleTypeDef hspi1;

#define LCD_BUF_PIXELS  512
static uint8_t lcd_buf[LCD_BUF_PIXELS * 3];

/* 不依赖 SysTick 的阻塞延时（用于 LCD_Init，调度器启动前调用）
 * HAL_Delay 依赖 SysTick 中断递增 uwTick，但 FreeRTOS 接管 SysTick 后
 * 调度器启动前 uwTick 不再递增，HAL_Delay 会死循环 */
static void LCD_DelayMs(volatile uint32_t ms)
{
    /* 168MHz 主频，大致按每毫秒消耗循环估算
     * 每次循环约 4~6 条指令，保守取 28000 次循环/ms */
    while (ms--)
    {
        for (volatile uint32_t i = 0; i < 28000; i++) { __NOP(); }
    }
}

lcd_dev_t g_lcd;
uint16_t g_point_color = BLACK;
uint16_t g_back_color = WHITE;

static void LCD_GPIO_Init(void)
{
    GPIO_InitTypeDef gpio = {0};

    __HAL_RCC_GPIOB_CLK_ENABLE();

    /* CS/DC/RST 仍用普通 GPIO 推挽输出
     * PB15 (LCD_LED) 不在此处初始化, 改由 LCD_BL_PWM_Init 配置为 TIM12_CH2 AF 模式 */
    gpio.Mode = GPIO_MODE_OUTPUT_PP;
    gpio.Pull = GPIO_PULLUP;
    gpio.Speed = GPIO_SPEED_FREQ_MEDIUM;
    gpio.Pin = LCD_CS_Pin | LCD_DC_Pin | LCD_RST_Pin;
    HAL_GPIO_Init(GPIOB, &gpio);

    LCD_CS_H();
    LCD_RST_H();
}

static void LCD_WR_REG(uint8_t cmd)
{
    LCD_CS_L();
    LCD_DC_L();
    HAL_SPI_Transmit(&hspi1, &cmd, 1, HAL_MAX_DELAY);
    LCD_CS_H();
}

static void LCD_WR_DATA(uint8_t data)
{
    LCD_CS_L();
    LCD_DC_H();
    HAL_SPI_Transmit(&hspi1, &data, 1, HAL_MAX_DELAY);
    LCD_CS_H();
}

static void LCD_WriteReg(uint8_t reg, uint16_t val)
{
    LCD_WR_REG(reg);
    LCD_WR_DATA(val);
}

static void Lcd_WriteData_16Bit(uint16_t data)
{
    uint8_t buf[3];
    buf[0] = (data >> 8) & 0xF8;
    buf[1] = (data >> 3) & 0xFC;
    buf[2] = data << 3;

    LCD_CS_L();
    LCD_DC_H();
    HAL_SPI_Transmit(&hspi1, buf, 3, HAL_MAX_DELAY);
    LCD_CS_H();
}

static void LCD_RESET(void)
{
    LCD_RST_L();
    LCD_DelayMs(100);
    LCD_RST_H();
    LCD_DelayMs(50);
}

void LCD_direction(uint8_t dir)
{
    g_lcd.setxcmd = 0x2A;
    g_lcd.setycmd = 0x2B;
    g_lcd.wramcmd = 0x2C;

    switch (dir)
    {
    case 0:
        g_lcd.width = LCD_W;
        g_lcd.height = LCD_H;
        LCD_WriteReg(0x36, (1 << 3) | (0 << 6) | (0 << 7));
        break;
    case 1:
        g_lcd.width = LCD_H;
        g_lcd.height = LCD_W;
        LCD_WriteReg(0x36, (1 << 3) | (0 << 7) | (1 << 6) | (1 << 5));
        break;
    case 2:
        g_lcd.width = LCD_W;
        g_lcd.height = LCD_H;
        LCD_WriteReg(0x36, (1 << 3) | (1 << 6) | (1 << 7));
        break;
    case 3:
        g_lcd.width = LCD_H;
        g_lcd.height = LCD_W;
        LCD_WriteReg(0x36, (1 << 3) | (1 << 7) | (1 << 5));
        break;
    default:
        break;
    }
}

void LCD_Init(void)
{
    hspi1.Instance = SPI1;
    hspi1.Init.Mode = SPI_MODE_MASTER;
    hspi1.Init.Direction = SPI_DIRECTION_2LINES;
    hspi1.Init.DataSize = SPI_DATASIZE_8BIT;
    hspi1.Init.CLKPolarity = SPI_POLARITY_LOW;
    hspi1.Init.CLKPhase = SPI_PHASE_1EDGE;
    hspi1.Init.NSS = SPI_NSS_SOFT;
    hspi1.Init.BaudRatePrescaler = SPI_BAUDRATEPRESCALER_8;
    hspi1.Init.FirstBit = SPI_FIRSTBIT_MSB;
    hspi1.Init.TIMode = SPI_TIMODE_DISABLE;
    hspi1.Init.CRCCalculation = SPI_CRCCALCULATION_DISABLE;
    hspi1.Init.CRCPolynomial = 7;
    HAL_SPI_Init(&hspi1);
    __HAL_SPI_ENABLE(&hspi1);

    LCD_GPIO_Init();
    LCD_RESET();

    LCD_WR_REG(0xF7);
    LCD_WR_DATA(0xA9);
    LCD_WR_DATA(0x51);
    LCD_WR_DATA(0x2C);
    LCD_WR_DATA(0x82);

    LCD_WR_REG(0xC0);
    LCD_WR_DATA(0x11);
    LCD_WR_DATA(0x09);

    LCD_WR_REG(0xC1);
    LCD_WR_DATA(0x41);

    LCD_WR_REG(0xC5);
    LCD_WR_DATA(0x00);
    LCD_WR_DATA(0x0A);
    LCD_WR_DATA(0x80);

    LCD_WR_REG(0xB1);
    LCD_WR_DATA(0xB0);
    LCD_WR_DATA(0x11);

    LCD_WR_REG(0xB4);
    LCD_WR_DATA(0x02);

    LCD_WR_REG(0xB6);
    LCD_WR_DATA(0x02);
    LCD_WR_DATA(0x42);

    LCD_WR_REG(0xB7);
    LCD_WR_DATA(0xC6);

    LCD_WR_REG(0xBE);
    LCD_WR_DATA(0x00);
    LCD_WR_DATA(0x04);

    LCD_WR_REG(0xE9);
    LCD_WR_DATA(0x00);

    LCD_WR_REG(0x36);
    LCD_WR_DATA((1 << 3) | (0 << 7) | (1 << 6) | (1 << 5));

    LCD_WR_REG(0x3A);
    LCD_WR_DATA(0x66);

    LCD_WR_REG(0xE0);
    LCD_WR_DATA(0x00);
    LCD_WR_DATA(0x07);
    LCD_WR_DATA(0x10);
    LCD_WR_DATA(0x09);
    LCD_WR_DATA(0x17);
    LCD_WR_DATA(0x0B);
    LCD_WR_DATA(0x41);
    LCD_WR_DATA(0x89);
    LCD_WR_DATA(0x4B);
    LCD_WR_DATA(0x0A);
    LCD_WR_DATA(0x0C);
    LCD_WR_DATA(0x0E);
    LCD_WR_DATA(0x18);
    LCD_WR_DATA(0x1B);
    LCD_WR_DATA(0x0F);

    LCD_WR_REG(0xE1);
    LCD_WR_DATA(0x00);
    LCD_WR_DATA(0x17);
    LCD_WR_DATA(0x1A);
    LCD_WR_DATA(0x04);
    LCD_WR_DATA(0x0E);
    LCD_WR_DATA(0x06);
    LCD_WR_DATA(0x2F);
    LCD_WR_DATA(0x45);
    LCD_WR_DATA(0x43);
    LCD_WR_DATA(0x02);
    LCD_WR_DATA(0x0A);
    LCD_WR_DATA(0x09);
    LCD_WR_DATA(0x32);
    LCD_WR_DATA(0x36);
    LCD_WR_DATA(0x0F);

    LCD_WR_REG(0x11);
    LCD_DelayMs(120);

    LCD_WR_REG(0x29);

    LCD_direction(USE_HORIZONTAL);
    LCD_BL_PWM_Init();              /* 启动背光 PWM, 默认 100% 亮度 */
    LCD_Clear(WHITE);
}

/* ============================================================
 * 背光 PWM 调光实现
 *
 * 硬件: PB15 复用为 TIM12_CH2 (AF9)
 * 时钟: TIM12 在 APB1, 定时器时钟 = 84MHz
 *       PSC=83 -> 计数时钟 1MHz, ARR=99 -> PWM 频率 10kHz
 * 占空比: CCR/100, CCR 范围 0~100 对应 0%~100% 亮度
 *
 * 默认假设高电平点亮 (与原 LCD_LED_ON()=SET 一致)
 * 若硬件经 MOSFET 反相, 把 OCMode 改 PWM2 或 OCPolarity 改 LOW 即可
 * ============================================================ */
static TIM_HandleTypeDef htim12;

void LCD_BL_PWM_Init(void)
{
    GPIO_InitTypeDef gpio = {0};
    TIM_OC_InitTypeDef oc = {0};

    /* 1. 使能时钟 */
    __HAL_RCC_TIM12_CLK_ENABLE();
    /* GPIOB 时钟已在 LCD_GPIO_Init 使能 */

    /* 2. 配置 PB15 为 AF9 (TIM12_CH2) */
    gpio.Pin = LCD_LED_Pin;                 /* PB15 */
    gpio.Mode = GPIO_MODE_AF_PP;
    gpio.Pull = GPIO_NOPULL;
    gpio.Speed = GPIO_SPEED_FREQ_LOW;
    gpio.Alternate = GPIO_AF9_TIM12;
    HAL_GPIO_Init(LCD_LED_Port, &gpio);

    /* 3. 配置 TIM12 时基
     *    PSC=83 -> 84MHz/(83+1)=1MHz, ARR=99 -> PWM=10kHz */
    htim12.Instance = TIM12;
    htim12.Init.Prescaler = 83;
    htim12.Init.CounterMode = TIM_COUNTERMODE_UP;
    htim12.Init.Period = 99;
    htim12.Init.ClockDivision = TIM_CLOCKDIVISION_DIV1;
    htim12.Init.AutoReloadPreload = TIM_AUTORELOAD_PRELOAD_ENABLE;
    if (HAL_TIM_PWM_Init(&htim12) != HAL_OK)
    {
        /* PWM 初始化失败: 退化为 GPIO 拉高, 保证屏可见 */
        GPIO_InitTypeDef g = {0};
        g.Pin = LCD_LED_Pin;
        g.Mode = GPIO_MODE_OUTPUT_PP;
        g.Pull = GPIO_PULLUP;
        g.Speed = GPIO_SPEED_FREQ_MEDIUM;
        HAL_GPIO_Init(LCD_LED_Port, &g);
        HAL_GPIO_WritePin(LCD_LED_Port, LCD_LED_Pin, GPIO_PIN_SET);
        return;
    }

    /* 4. 配置 CH2 输出比较 (PWM mode 1: CNT<CCR 时输出高, 高电平点亮) */
    oc.OCMode = TIM_OCMODE_PWM1;
    oc.Pulse = 100;                         /* 默认 100% 亮度 */
    oc.OCPolarity = TIM_OCPOLARITY_HIGH;
    oc.OCFastMode = TIM_OCFAST_DISABLE;
    HAL_TIM_PWM_ConfigChannel(&htim12, &oc, TIM_CHANNEL_2);

    /* 5. 启动 PWM 输出 */
    HAL_TIM_PWM_Start(&htim12, TIM_CHANNEL_2);
}

void LCD_BL_SetBrightness(uint32_t v)
{
    /* 范围限制: 0~100, 超出按 100 处理 */
    if (v > 100) v = 100;
    /* CCR=v 直接对应占空比 v% (ARR=99, CCR=100 时全高=100% 占空比) */
    __HAL_TIM_SET_COMPARE(&htim12, TIM_CHANNEL_2, v);
}

void LCD_SetWindows(uint16_t x1, uint16_t y1, uint16_t x2, uint16_t y2)
{
    LCD_WR_REG(g_lcd.setxcmd);
    LCD_WR_DATA(x1 >> 8);
    LCD_WR_DATA(x1 & 0xFF);
    LCD_WR_DATA(x2 >> 8);
    LCD_WR_DATA(x2 & 0xFF);

    LCD_WR_REG(g_lcd.setycmd);
    LCD_WR_DATA(y1 >> 8);
    LCD_WR_DATA(y1 & 0xFF);
    LCD_WR_DATA(y2 >> 8);
    LCD_WR_DATA(y2 & 0xFF);

    LCD_WR_REG(g_lcd.wramcmd);
}

void LCD_SetCursor(uint16_t x, uint16_t y)
{
    LCD_SetWindows(x, y, x, y);
}

void LCD_DrawPoint(uint16_t x, uint16_t y)
{
    LCD_SetCursor(x, y);
    Lcd_WriteData_16Bit(g_point_color);
}

static void lcd_buf_fill_color(uint16_t color)
{
    uint32_t i;
    uint8_t r = (color >> 8) & 0xF8;
    uint8_t g = (color >> 3) & 0xFC;
    uint8_t b = color << 3;
    for (i = 0; i + 3 <= sizeof(lcd_buf); i += 3)
    {
        lcd_buf[i] = r;
        lcd_buf[i + 1] = g;
        lcd_buf[i + 2] = b;
    }
}

void LCD_Clear(uint16_t color)
{
    uint32_t remaining = (uint32_t)g_lcd.width * g_lcd.height;
    uint32_t chunk;

    lcd_buf_fill_color(color);
    LCD_SetWindows(0, 0, g_lcd.width - 1, g_lcd.height - 1);
    LCD_CS_L();
    LCD_DC_H();
    while (remaining)
    {
        chunk = (remaining > LCD_BUF_PIXELS) ? LCD_BUF_PIXELS : remaining;
        HAL_SPI_Transmit(&hspi1, lcd_buf, chunk * 3, HAL_MAX_DELAY);
        remaining -= chunk;
    }
    LCD_CS_H();
}

void LCD_Fill(uint16_t x1, uint16_t y1, uint16_t x2, uint16_t y2, uint16_t color)
{
    uint32_t remaining = (uint32_t)(x2 - x1 + 1) * (y2 - y1 + 1);
    uint32_t chunk;

    lcd_buf_fill_color(color);
    LCD_SetWindows(x1, y1, x2, y2);
    LCD_CS_L();
    LCD_DC_H();
    while (remaining)
    {
        chunk = (remaining > LCD_BUF_PIXELS) ? LCD_BUF_PIXELS : remaining;
        HAL_SPI_Transmit(&hspi1, lcd_buf, chunk * 3, HAL_MAX_DELAY);
        remaining -= chunk;
    }
    LCD_CS_H();
}

void LCD_DrawLine(uint16_t x1, uint16_t y1, uint16_t x2, uint16_t y2)
{
    int16_t dx = (x2 > x1) ? (x2 - x1) : (x1 - x2);
    int16_t dy = (y2 > y1) ? (y2 - y1) : (y1 - y2);
    int16_t sx = (x2 >= x1) ? 1 : -1;
    int16_t sy = (y2 >= y1) ? 1 : -1;
    int16_t err = dx - dy;
    int16_t e2;

    while (1)
    {
        LCD_DrawPoint(x1, y1);
        if (x1 == x2 && y1 == y2) break;
        e2 = 2 * err;
        if (e2 > -dy) { err -= dy; x1 += sx; }
        if (e2 < dx)  { err += dx; y1 += sy; }
    }
}

void LCD_ShowColorBar(void)
{
    uint16_t colors[] = {RED, GREEN, BLUE, YELLOW, CYAN, MAGENTA, WHITE, BLACK};
    uint8_t i;
    uint16_t bar_h = g_lcd.height / 8;

    for (i = 0; i < 8; i++)
    {
        LCD_Fill(0, i * bar_h, g_lcd.width - 1, (i + 1) * bar_h - 1, colors[i]);
    }
}
