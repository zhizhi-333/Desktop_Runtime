#include "settings.h"
#include "stm32f4xx_hal.h"
#include "main.h"

/* ============================================================
 * 设置模块实现
 *
 * Flash 存储布局：
 *   地址 0x080E0000（Sector 11）存储 settings_data_t
 *   写入前需先擦除整个扇区
 * ============================================================ */

/* Flash 存储地址（Sector 11 起始地址） */
#define SETTINGS_FLASH_ADDR     ((uint32_t)0x080E0000)
#define SETTINGS_FLASH_SECTOR   FLASH_SECTOR_11

/* 魔数，用于判断 Flash 中是否有有效数据 */
#define SETTINGS_MAGIC          0x53455431  /* "SET1" */

/* ---- 模块状态 ---- */
static settings_data_t g_settings;

/* ---- 范围限制 ---- */
static uint32_t clamp(uint32_t v, uint32_t lo, uint32_t hi)
{
    if (v < lo) return lo;
    if (v > hi) return hi;
    return v;
}

/* ---- 从 Flash 读取设置 ---- */
static int load_from_flash(void)
{
    settings_data_t *flash = (settings_data_t *)SETTINGS_FLASH_ADDR;

    /* 检查魔数 */
    if (flash->magic != SETTINGS_MAGIC)
        return -1;  /* Flash 中无有效数据 */

    /* 复制到内存 */
    g_settings = *flash;

    /* 校验范围（防止 Flash 数据损坏） */
    g_settings.cursor_sensitivity = clamp(g_settings.cursor_sensitivity, SENSITIVITY_MIN, SENSITIVITY_MAX);
    g_settings.cursor_size        = clamp(g_settings.cursor_size,        CURSOR_SIZE_MIN,  CURSOR_SIZE_MAX);
    g_settings.brightness         = clamp(g_settings.brightness,         BRIGHTNESS_MIN,   BRIGHTNESS_MAX);
    g_settings.volume             = clamp(g_settings.volume,             VOLUME_MIN,       VOLUME_MAX);
    g_settings.screen_timeout     = clamp(g_settings.screen_timeout,     TIMEOUT_MIN,      TIMEOUT_MAX);

    return 0;
}

/* ---- 写入 Flash ---- */
static int save_to_flash(void)
{
    HAL_StatusTypeDef status;
    FLASH_EraseInitTypeDef erase;
    uint32_t err;
    uint32_t *src;
    int i;

    /* 1. 解锁 Flash */
    status = HAL_FLASH_Unlock();
    if (status != HAL_OK) return -1;

    /* 2. 擦除 Sector 11 */
    erase.TypeErase    = FLASH_TYPEERASE_SECTORS;
    erase.Sector       = SETTINGS_FLASH_SECTOR;
    erase.NbSectors    = 1;
    erase.VoltageRange = FLASH_VOLTAGE_RANGE_3;
    status = HAL_FLASHEx_Erase(&erase, &err);
    if (status != HAL_OK)
    {
        HAL_FLASH_Lock();
        return -1;
    }

    /* 3. 逐字写入 */
    src = (uint32_t *)&g_settings;
    for (i = 0; i < (int)(sizeof(settings_data_t) / 4); i++)
    {
        status = HAL_FLASH_Program(FLASH_TYPEPROGRAM_WORD,
                                    SETTINGS_FLASH_ADDR + i * 4,
                                    src[i]);
        if (status != HAL_OK)
        {
            HAL_FLASH_Lock();
            return -1;
        }
    }

    /* 4. 锁定 Flash */
    HAL_FLASH_Lock();
    return 0;
}

/* ============================================================
 * 公共接口
 * ============================================================ */

void Settings_Init(void)
{
    if (load_from_flash() != 0)
    {
        /* Flash 无有效数据，用默认值 */
        Settings_ResetDefault();
        /* 立即保存到 Flash */
        save_to_flash();
    }

    /* 应用亮度设置 */
    Settings_SetBrightness(g_settings.brightness);
}

const settings_data_t *Settings_Get(void)
{
    return &g_settings;
}

int Settings_SetCursorSensitivity(uint32_t v)
{
    v = clamp(v, SENSITIVITY_MIN, SENSITIVITY_MAX);
    if (g_settings.cursor_sensitivity == v) return 1;
    g_settings.cursor_sensitivity = v;
    return 0;
}

int Settings_SetCursorSize(uint32_t v)
{
    v = clamp(v, CURSOR_SIZE_MIN, CURSOR_SIZE_MAX);
    if (g_settings.cursor_size == v) return 1;
    g_settings.cursor_size = v;
    return 0;
}

int Settings_SetBrightness(uint32_t v)
{
    v = clamp(v, BRIGHTNESS_MIN, BRIGHTNESS_MAX);
    if (g_settings.brightness == v) return 1;
    g_settings.brightness = v;

    /* 应用亮度：0=关背光，>0=开背光（当前 GPIO 只有 on/off） */
    if (v == 0)
        HAL_GPIO_WritePin(LCD_BL_GPIO_Port, LCD_BL_Pin, GPIO_PIN_RESET);
    else
        HAL_GPIO_WritePin(LCD_BL_GPIO_Port, LCD_BL_Pin, GPIO_PIN_SET);

    return 0;
}

int Settings_SetVolume(uint32_t v)
{
    v = clamp(v, VOLUME_MIN, VOLUME_MAX);
    if (g_settings.volume == v) return 1;
    g_settings.volume = v;
    return 0;
}

int Settings_SetScreenTimeout(uint32_t v)
{
    v = clamp(v, TIMEOUT_MIN, TIMEOUT_MAX);
    if (g_settings.screen_timeout == v) return 1;
    g_settings.screen_timeout = v;
    return 0;
}

int Settings_Save(void)
{
    return save_to_flash();
}

void Settings_ResetDefault(void)
{
    g_settings.magic              = SETTINGS_MAGIC;
    g_settings.cursor_sensitivity = SETTINGS_DEFAULT_CURSOR_SENSITIVITY;
    g_settings.cursor_size        = SETTINGS_DEFAULT_CURSOR_SIZE;
    g_settings.brightness         = SETTINGS_DEFAULT_BRIGHTNESS;
    g_settings.volume             = SETTINGS_DEFAULT_VOLUME;
    g_settings.screen_timeout     = SETTINGS_DEFAULT_SCREEN_TIMEOUT;
}

/* ---- 快捷访问 ---- */
uint32_t Settings_CursorSensitivity(void) { return g_settings.cursor_sensitivity; }
uint32_t Settings_CursorSize(void)        { return g_settings.cursor_size; }
uint32_t Settings_Brightness(void)        { return g_settings.brightness; }
uint32_t Settings_Volume(void)            { return g_settings.volume; }
uint32_t Settings_ScreenTimeout(void)     { return g_settings.screen_timeout; }
