#include "ds1302.h"
#include "stm32f4xx_hal.h"
#include "usart.h"

/* ============================================================
 * DS1302 寄存器命令字节
 *   bit7=1 (固定), bit6=0(clock/calendar)/1(RAM), bit5-bit1=地址, bit0=0(写)/1(读)
 * ============================================================ */
#define DS1302_CMD_WRITE_SEC    0x80    /* 秒(写), bit7=CH 停摆位 */
#define DS1302_CMD_READ_SEC     0x81    /* 秒(读) */
#define DS1302_CMD_WRITE_MIN    0x82
#define DS1302_CMD_READ_MIN     0x83
#define DS1302_CMD_WRITE_HOUR   0x84    /* bit7=0(24h)/1(12h) */
#define DS1302_CMD_READ_HOUR    0x85
#define DS1302_CMD_WRITE_DATE   0x86
#define DS1302_CMD_READ_DATE    0x87
#define DS1302_CMD_WRITE_MONTH  0x88
#define DS1302_CMD_READ_MONTH   0x89
#define DS1302_CMD_WRITE_DAY    0x8A    /* 周几 */
#define DS1302_CMD_READ_DAY     0x8B
#define DS1302_CMD_WRITE_YEAR   0x8C
#define DS1302_CMD_READ_YEAR    0x8D
#define DS1302_CMD_WRITE_CTRL   0x8E    /* bit7=WP 写保护 */
#define DS1302_CMD_READ_CTRL    0x8F
#define DS1302_CMD_WRITE_TCR    0x90    /* 涓流充电寄存器 */
#define DS1302_CMD_READ_TCR     0x91

/* 引脚定义：PC0=CLK, PC1=DAT, PC2=RST */
#define DS1302_CLK_Port         GPIOC
#define DS1302_CLK_Pin          GPIO_PIN_0
#define DS1302_DAT_Port         GPIOC
#define DS1302_DAT_Pin          GPIO_PIN_1
#define DS1302_RST_Port         GPIOC
#define DS1302_RST_Pin          GPIO_PIN_2

/* ---------- BCD 转换 ---------- */
static uint8_t bcd_to_bin(uint8_t bcd)
{
    return (uint8_t)(((bcd >> 4) & 0x0F) * 10 + (bcd & 0x0F));
}

static uint8_t bin_to_bcd(uint8_t bin)
{
    return (uint8_t)(((bin / 10) << 4) | (bin % 10));
}

/* ---------- 底层 GPIO 操作 ---------- */
static void clk_low(void)   { HAL_GPIO_WritePin(DS1302_CLK_Port, DS1302_CLK_Pin, GPIO_PIN_RESET); }
static void clk_high(void)  { HAL_GPIO_WritePin(DS1302_CLK_Port, DS1302_CLK_Pin, GPIO_PIN_SET); }
static void rst_low(void)   { HAL_GPIO_WritePin(DS1302_RST_Port, DS1302_RST_Pin, GPIO_PIN_RESET); }
static void rst_high(void)  { HAL_GPIO_WritePin(DS1302_RST_Port, DS1302_RST_Pin, GPIO_PIN_SET); }
static void dat_out_high(void) { HAL_GPIO_WritePin(DS1302_DAT_Port, DS1302_DAT_Pin, GPIO_PIN_SET); }
static void dat_out_low(void)  { HAL_GPIO_WritePin(DS1302_DAT_Port, DS1302_DAT_Pin, GPIO_PIN_RESET); }

static void dat_input(void)
{
    /* 切换 DAT 为输入上拉，便于 DS1302 输出数据 */
    GPIO_InitTypeDef g = {0};
    g.Pin = DS1302_DAT_Pin;
    g.Mode = GPIO_MODE_INPUT;
    g.Pull = GPIO_PULLUP;
    g.Speed = GPIO_SPEED_FREQ_LOW;
    HAL_GPIO_Init(DS1302_DAT_Port, &g);
}

static void dat_output(void)
{
    /* 切换 DAT 为推挽输出 */
    GPIO_InitTypeDef g = {0};
    g.Pin = DS1302_DAT_Pin;
    g.Mode = GPIO_MODE_OUTPUT_PP;
    g.Pull = GPIO_NOPULL;
    g.Speed = GPIO_SPEED_FREQ_LOW;
    HAL_GPIO_Init(DS1302_DAT_Port, &g);
}

static uint8_t dat_read(void)
{
    return (HAL_GPIO_ReadPin(DS1302_DAT_Port, DS1302_DAT_Pin) == GPIO_PIN_SET) ? 1 : 0;
}

/* 短延时：约 1us @ 168MHz (DS1302 SCLK 最大 2MHz, 需要时序余量) */
static void delay_short(void)
{
    volatile int d;
    for (d = 0; d < 50; d++);
}

/* ---------- DS1302 协议层 ---------- */

/* 写一个字节（命令或数据），LSB 先发，每个 bit 在 SCLK 上升沿锁存 */
static void ds1302_write_byte(uint8_t b)
{
    int i;
    dat_output();
    for (i = 0; i < 8; i++)
    {
        clk_low();
        delay_short();
        if (b & (1 << i)) dat_out_high();
        else dat_out_low();
        delay_short();
        clk_high();          /* 上升沿锁存数据 */
        delay_short();
    }
    clk_low();
}

/* 读一个字节，LSB 先收，每个 bit 在 SCLK 下降沿后数据有效 */
static uint8_t ds1302_read_byte(void)
{
    uint8_t b = 0;
    int i;
    dat_input();
    for (i = 0; i < 8; i++)
    {
        clk_high();
        delay_short();
        clk_low();           /* 下降沿后 DS1302 输出 bit */
        delay_short();
        if (dat_read()) b |= (uint8_t)(1 << i);
    }
    return b;
}

/* 单次访问：写命令 + 写数据 */
static void ds1302_write_register(uint8_t cmd, uint8_t val)
{
    rst_high();
    delay_short();
    ds1302_write_byte(cmd);
    ds1302_write_byte(val);
    rst_low();
    delay_short();
}

/* 单次访问：写命令 + 读数据
 *
 * DS1302 读时序（关键）：
 *   1. CE 拉高
 *   2. 主机写 8 位命令字节，每个 bit 在 SCLK 上升沿锁存
 *   3. 命令字节最后一个 bit (bit7) 的 SCLK 上升沿后，SCLK 保持高
 *   4. SCLK 下降沿（高→低）让 DS1302 输出第一个数据 bit (bit0)
 *   5. 之后每个 SCLK 上升沿准备下一个 bit，下降沿输出下一个 bit
 *
 * 之前的 bug：ds1302_write_byte 结束时 clk_low() 产生了 bit0 的下降沿，
 * 但当时 DAT 还在输出模式，bit0 被丢失。后续 ds1302_read_byte 的循环
 * 从 bit1 开始读，所有数据右移一位，导致时间完全错乱。
 *
 * 修复：内联整个读过程，写命令时最后一个 SCLK 上升沿后保持高，
 * 切换 DAT 为输入后再产生下降沿读 bit0。 */
static uint8_t ds1302_read_register(uint8_t cmd)
{
    uint8_t val = 0;
    int i;

    rst_high();
    delay_short();

    /* 写命令字节：每个 bit 在 SCLK 上升沿锁存，结束时 SCLK 保持高 */
    dat_output();
    for (i = 0; i < 8; i++)
    {
        clk_low();
        delay_short();
        if (cmd & (1 << i)) dat_out_high();
        else dat_out_low();
        delay_short();
        clk_high();          /* 上升沿锁存 */
        delay_short();
    }
    /* 注意：这里不 clk_low()，SCLK 保持高，等待读阶段产生第一个下降沿 */

    /* 切换 DAT 为输入 */
    dat_input();
    delay_short();

    /* 读 8 位数据：每个 SCLK 下降沿后 DS1302 输出一个 bit */
    for (i = 0; i < 8; i++)
    {
        clk_low();           /* 下降沿：DS1302 输出 bit i */
        delay_short();
        if (dat_read()) val |= (uint8_t)(1 << i);
        clk_high();          /* 上升沿：准备下一个 bit */
        delay_short();
    }
    clk_low();               /* 结束时 SCLK 拉低 */

    rst_low();
    delay_short();
    return val;
}

/* ---------- 公开接口 ---------- */

void DS1302_Init(void)
{
    GPIO_InitTypeDef g = {0};

    __HAL_RCC_GPIOC_CLK_ENABLE();

    /* CLK / RST: 推挽输出，默认低电平 */
    g.Mode = GPIO_MODE_OUTPUT_PP;
    g.Pull = GPIO_NOPULL;
    g.Speed = GPIO_SPEED_FREQ_LOW;
    g.Pin = DS1302_CLK_Pin | DS1302_RST_Pin;
    HAL_GPIO_Init(GPIOC, &g);
    clk_low();
    rst_low();

    /* DAT: 初始为输入（按需切换方向） */
    g.Pin = DS1302_DAT_Pin;
    g.Mode = GPIO_MODE_INPUT;
    g.Pull = GPIO_PULLUP;
    HAL_GPIO_Init(GPIOC, &g);

    delay_short();

    /* 关闭写保护，允许写入时间寄存器 */
    ds1302_write_register(DS1302_CMD_WRITE_CTRL, 0x00);

    /* 关闭涓流充电：避免锂电池过充（用户模块通常已带 CR2032 一次性电池） */
    ds1302_write_register(DS1302_CMD_WRITE_TCR, 0x00);

    /* 再次关闭写保护（关闭涓流充电后写保护可能被某些模块恢复，确保一致） */
    ds1302_write_register(DS1302_CMD_WRITE_CTRL, 0x00);

    Log_Printf("[DS1302] init OK (CLK=PC0, DAT=PC1, RST=PC2)\r\n");
    if (DS1302_IsHalted())
        Log_Printf("[DS1302] halted (CH=1), need to set time\r\n");
    else
        Log_Printf("[DS1302] running, time preserved by battery\r\n");
}

uint8_t DS1302_IsHalted(void)
{
    /* 读秒寄存器，bit7 = CH 停摆位 */
    uint8_t sec_reg = ds1302_read_register(DS1302_CMD_READ_SEC);
    return (sec_reg & 0x80) ? 1 : 0;
}

void DS1302_ReadTime(uint8_t *hour, uint8_t *min, uint8_t *sec)
{
    uint8_t h, m, s;
    h = ds1302_read_register(DS1302_CMD_READ_HOUR);
    m = ds1302_read_register(DS1302_CMD_READ_MIN);
    s = ds1302_read_register(DS1302_CMD_READ_SEC);

    /* HOUR bit7/bit6 在 24h 模式下是保留位，用 0x3F 屏蔽 */
    h = bcd_to_bin((uint8_t)(h & 0x3F));
    m = bcd_to_bin((uint8_t)(m & 0x7F));
    s = bcd_to_bin((uint8_t)(s & 0x7F));

    if (hour) *hour = h;
    if (min)  *min  = m;
    if (sec)  *sec  = s;
}

void DS1302_WriteTime(uint8_t hour, uint8_t min, uint8_t sec)
{
    /* 边界保护 */
    if (hour >= 24) hour = 23;
    if (min  >= 60) min  = 59;
    if (sec  >= 60) sec  = 59;

    /* 1. 关写保护 */
    ds1302_write_register(DS1302_CMD_WRITE_CTRL, 0x00);

    /* 2. 写秒（CH=0 启动走时） */
    ds1302_write_register(DS1302_CMD_WRITE_SEC, bin_to_bcd(sec));

    /* 3. 写分 */
    ds1302_write_register(DS1302_CMD_WRITE_MIN, bin_to_bcd(min));

    /* 4. 写时（24h 模式，bit7=0） */
    ds1302_write_register(DS1302_CMD_WRITE_HOUR, (uint8_t)(bin_to_bcd(hour) & 0x7F));

    /* 5. 开写保护（防止误写） */
    ds1302_write_register(DS1302_CMD_WRITE_CTRL, 0x80);

    Log_Printf("[DS1302] time set: %02d:%02d:%02d\r\n", hour, min, sec);
}
