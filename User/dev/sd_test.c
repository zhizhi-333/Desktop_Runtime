#include "sd_test.h"
#include "lcd.h"
#include "stm32f4xx_hal.h"

SD_HandleTypeDef hsd;

static void MX_SDIO_GPIO_Init(void)
{
    GPIO_InitTypeDef gpio = {0};

    __HAL_RCC_GPIOC_CLK_ENABLE();
    __HAL_RCC_GPIOD_CLK_ENABLE();
    __HAL_RCC_SDIO_CLK_ENABLE();

    gpio.Mode = GPIO_MODE_AF_PP;
    gpio.Pull = GPIO_PULLUP;
    gpio.Speed = GPIO_SPEED_FREQ_HIGH;
    gpio.Alternate = GPIO_AF12_SDIO;

    gpio.Pin = GPIO_PIN_8 | GPIO_PIN_9 | GPIO_PIN_10 | GPIO_PIN_11 | GPIO_PIN_12;
    HAL_GPIO_Init(GPIOC, &gpio);

    gpio.Pin = GPIO_PIN_2;
    HAL_GPIO_Init(GPIOD, &gpio);
}

void SD_Test(void)
{
    MX_SDIO_GPIO_Init();

    HAL_NVIC_SetPriority(SDIO_IRQn, 0x0E, 0);
    HAL_NVIC_EnableIRQ(SDIO_IRQn);

    hsd.Instance = SDIO;
    hsd.Init.ClockEdge = SDIO_CLOCK_EDGE_RISING;
    hsd.Init.ClockBypass = SDIO_CLOCK_BYPASS_DISABLE;
    hsd.Init.ClockPowerSave = SDIO_CLOCK_POWER_SAVE_DISABLE;
    hsd.Init.BusWide = SDIO_BUS_WIDE_1B;
    hsd.Init.HardwareFlowControl = SDIO_HARDWARE_FLOW_CONTROL_DISABLE;
    hsd.Init.ClockDiv = 0;

    if (HAL_SD_Init(&hsd) == HAL_OK)
    {
        LCD_Fill(10, 285, 190, 305, 0x07E0);
    }
    else
    {
        LCD_Fill(10, 285, 190, 305, 0xF800);
    }
}
