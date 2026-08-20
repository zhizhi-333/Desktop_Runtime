#include "settings.h"
#include "stm32f4xx_hal.h"
#include "main.h"
#include "usart.h"
#include "lcd.h"
#include "FreeRTOS.h"
#include "task.h"
#include "semphr.h"
#include <stddef.h>

/* ============================================================
 * 设置模块实现
 *
 * Flash 存储布局：
 *   地址 0x080E0000（Sector 11）存储 settings_data_t
 *   写入前需先擦除整个扇区
 *
 * 同步保护: 互斥量 settings_mutex
 *   - 多任务可能同时读写设置(MusicTask 改音量, InputTask 改其他)
 *   - Flash 写入期间禁止读取,避免读到中间态
 * ============================================================ */

/* Flash 存储地址（Sector 11 起始地址） */
#define SETTINGS_FLASH_ADDR     ((uint32_t)0x080E0000)
#define SETTINGS_FLASH_SECTOR   FLASH_SECTOR_11

/* 魔数，用于判断 Flash 中是否有有效数据 */
#define SETTINGS_MAGIC          0x53455431  /* "SET1" */

/* ---- 模块状态 ---- */
static settings_data_t g_settings;
static SemaphoreHandle_t settings_mutex = NULL;

/* ---- 范围限制 ---- */
static uint32_t clamp(uint32_t v, uint32_t lo, uint32_t hi)
{
    if (v < lo) return lo;
    if (v > hi) return hi;
    return v;
}

/* ---- CRC32 (多项式 0xEDB88320, 与 zlib/zip 一致) ----
 * 用于校验 Flash 中设置数据的完整性, 防止掉电写入不完整导致数据损坏 */
#define SETTINGS_CRC_POLY  0xEDB88320u
static uint32_t settings_crc32(const uint32_t *data, int words)
{
    uint32_t crc = 0xFFFFFFFFu;
    const uint8_t *p = (const uint8_t *)data;
    int i;
    for (i = 0; i < words * 4; i++)
    {
        crc ^= p[i];
        int b;
        for (b = 0; b < 8; b++)
        {
            if (crc & 1u)
                crc = (crc >> 1) ^ SETTINGS_CRC_POLY;
            else
                crc = (crc >> 1);
        }
    }
    return ~crc;
}

/* 计算设置数据的 CRC（覆盖 magic..screen_timeout, 不含 crc 字段） */
static uint32_t settings_compute_crc(const settings_data_t *s)
{
    return settings_crc32((const uint32_t *)s,
                          (int)(offsetof(settings_data_t, crc) / 4));
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

    /* CRC 校验：防止掉电/写入不完整导致数据损坏
     * 旧版数据(无 CRC 字段)或损坏数据都会校验失败 -> 返回 -1 由上层恢复默认 */
    if (g_settings.crc != settings_compute_crc(&g_settings))
    {
        Log_Printf("[SET] CRC mismatch! flash data corrupted, using defaults\r\n");
        return -1;
    }

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

    /* 0. 计算并填入 CRC（覆盖 magic..screen_timeout, 不含 crc 字段） */
    g_settings.crc = settings_compute_crc(&g_settings);

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
    /* 调度器启动前创建 mutex */
    settings_mutex = xSemaphoreCreateMutex();

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

    /* 应用亮度: 直接设置 PWM 占空比 (0=关背光, 1~100=对应亮度) */
    LCD_BL_SetBrightness(v);

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
    int ret;
    /* 调度器未启动时直接写(初始化阶段) */
    if (xTaskGetSchedulerState() == taskSCHEDULER_NOT_STARTED || settings_mutex == NULL)
        return save_to_flash();

    if (xSemaphoreTake(settings_mutex, pdMS_TO_TICKS(100)) == pdTRUE)
    {
        ret = save_to_flash();
        xSemaphoreGive(settings_mutex);
        return ret;
    }
    return -1;  /* 超时 */
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
