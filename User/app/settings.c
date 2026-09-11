#include "settings.h"
#include "stm32f4xx_hal.h"
#include "main.h"
#include "usart.h"
#include "lcd.h"
#include "dac.h"
#include "FreeRTOS.h"
#include "task.h"
#include "semphr.h"
#include <stddef.h>

/* ============================================================
 * 设置模块实现
 *
 * Flash 双槽存储布局:
 *   槽A: Sector 10 (0x080C0000) - 序号较旧时被覆写
 *   槽B: Sector 11 (0x080E0000) - 序号较旧时被覆写
 *   每槽存 settings_data_t (magic + 数据 + seq + crc + commit)
 *
 * 双槽掉电保护:
 *   保存: 选序号较旧的槽, 擦除→写数据(commit=0)→最后写commit标记
 *   启动: 两槽都读, 选 commit 有效 + CRC 正确 + seq 最新者
 *   掉电发生在写入中时, 旧槽仍可用
 *
 * 同步保护: 互斥量 settings_mutex
 *   - 多任务可能同时读写设置(MusicTask 改音量, InputTask 改其他)
 *   - Flash 写入期间禁止读取,避免读到中间态
 * ============================================================ */

/* 双槽地址与扇区 */
#define SETTINGS_SLOT_A_ADDR    ((uint32_t)0x080C0000)
#define SETTINGS_SLOT_A_SECTOR  FLASH_SECTOR_10
#define SETTINGS_SLOT_B_ADDR    ((uint32_t)0x080E0000)
#define SETTINGS_SLOT_B_SECTOR  FLASH_SECTOR_11

/* 魔数，用于判断 Flash 中是否有有效数据 */
#define SETTINGS_MAGIC          0x53455431  /* "SET1" */
/* 提交标记：写入完成后最后写此值，作为"写入完成"标记 */
#define SETTINGS_COMMITTED     0xC0FFEE0Au

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

/* 计算设置数据的 CRC（覆盖 magic..seq, 不含 crc 和 commit 字段） */
static uint32_t settings_compute_crc(const settings_data_t *s)
{
    return settings_crc32((const uint32_t *)s,
                          (int)(offsetof(settings_data_t, crc) / 4));
}

/* ---- 校验单个槽是否有效 ----
 * 返回 1=有效, 0=无效(magic/CRC/commit 任一不通过)
 * 有效时把数据复制到 out */
static int load_slot(uint32_t addr, settings_data_t *out)
{
    settings_data_t *flash = (settings_data_t *)addr;

    /* 1. 校验魔数 */
    if (flash->magic != SETTINGS_MAGIC)
        return 0;

    /* 2. 复制到内存（避免直接读 Flash 中间态） */
    *out = *flash;

    /* 3. CRC 校验：覆盖 magic..seq, 不含 crc/commit */
    if (out->crc != settings_compute_crc(out))
    {
        Log_Printf("[SET] slot@0x%08x CRC mismatch\r\n", (unsigned)addr);
        return 0;
    }

    /* 4. commit 标记校验：最后写入, 掉电时可能为 0 */
    if (out->commit != SETTINGS_COMMITTED)
    {
        Log_Printf("[SET] slot@0x%08x not committed (commit=0x%08x)\r\n",
                   (unsigned)addr, (unsigned)out->commit);
        return 0;
    }

    return 1;  /* 有效 */
}

/* ---- 从 Flash 读取设置(双槽择优) ----
 * 返回 0=成功, -1=两槽都无效(用默认值) */
static int load_from_flash(void)
{
    settings_data_t a, b;
    int a_valid, b_valid;

    a_valid = load_slot(SETTINGS_SLOT_A_ADDR, &a);
    b_valid = load_slot(SETTINGS_SLOT_B_ADDR, &b);

    if (a_valid && b_valid)
    {
        /* 两槽都有效: 选 seq 较新者(相等则选 B, 因为 B 是最近写的) */
        if (b.seq >= a.seq)
            g_settings = b;
        else
            g_settings = a;
        Log_Printf("[SET] both slots valid, using seq=%u\r\n",
                   (unsigned)g_settings.seq);
    }
    else if (a_valid)
    {
        g_settings = a;
        Log_Printf("[SET] only slot A valid, seq=%u\r\n",
                   (unsigned)g_settings.seq);
    }
    else if (b_valid)
    {
        g_settings = b;
        Log_Printf("[SET] only slot B valid, seq=%u\r\n",
                   (unsigned)g_settings.seq);
    }
    else
    {
        /* 两槽都无效(首次上电或都损坏) */
        Log_Printf("[SET] no valid slot, using defaults\r\n");
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

/* ---- 写入单个槽 ----
 * 流程: 擦除扇区→写整个结构(commit=0)→最后单独写commit标记
 * 掉电发生在写数据中: commit=0, 启动时该槽判无效, 旧槽仍可用
 * 掉电发生在写commit前: 同上, 旧槽仍可用
 * 只有 commit 写完后才算写入成功 */
static int save_slot(uint32_t addr, uint32_t sector)
{
    HAL_StatusTypeDef status;
    FLASH_EraseInitTypeDef erase;
    uint32_t err;
    const uint32_t *src;
    int i;

    /* 1. 解锁 Flash */
    status = HAL_FLASH_Unlock();
    if (status != HAL_OK) return -1;

    /* 2. 擦除目标扇区 */
    erase.TypeErase    = FLASH_TYPEERASE_SECTORS;
    erase.Sector       = sector;
    erase.NbSectors    = 1;
    erase.VoltageRange = FLASH_VOLTAGE_RANGE_3;
    status = HAL_FLASHEx_Erase(&erase, &err);
    if (status != HAL_OK)
    {
        HAL_FLASH_Lock();
        return -1;
    }

    /* 3. 逐字写入结构(magic..crc, 不含 commit 字段)
     * commit 字段留空(擦除后为 0xFFFFFFFF), 第4步单独写
     * 原因: Flash 只能 1→0, 若先写 commit=0 再写 0xC0FFEE0A 会失败 */
    src = (const uint32_t *)&g_settings;
    for (i = 0; i < (int)(offsetof(settings_data_t, commit) / 4); i++)
    {
        status = HAL_FLASH_Program(FLASH_TYPEPROGRAM_WORD,
                                    addr + i * 4,
                                    src[i]);
        if (status != HAL_OK)
        {
            HAL_FLASH_Lock();
            return -1;
        }
    }

    /* 4. 最后单独写 commit 标记 (原子操作: 写完才认为该槽有效)
     * commit 字段在结构末尾, 偏移 = offsetof(commit)
     * Flash 擦除后为 0xFFFFFFFF → 写 0xC0FFEE0A 合法(1→0) */
    status = HAL_FLASH_Program(FLASH_TYPEPROGRAM_WORD,
                                addr + offsetof(settings_data_t, commit),
                                SETTINGS_COMMITTED);
    if (status != HAL_OK)
    {
        HAL_FLASH_Lock();
        return -1;
    }

    /* 5. 锁定 Flash */
    HAL_FLASH_Lock();
    return 0;
}

/* ---- 写入 Flash(双槽: 写序号较旧的槽) ---- */
static int save_to_flash(void)
{
    settings_data_t a, b;
    int a_valid, b_valid;
    uint32_t new_seq;
    int use_b;   /* 1=写槽B, 0=写槽A */

    /* 1. 读取两槽当前状态(用于确定新 seq 和选旧槽) */
    a_valid = load_slot(SETTINGS_SLOT_A_ADDR, &a);
    b_valid = load_slot(SETTINGS_SLOT_B_ADDR, &b);

    /* 2. 计算新 seq = max(A.seq, B.seq) + 1 */
    if (a_valid && b_valid)
        new_seq = (a.seq >= b.seq ? a.seq : b.seq) + 1;
    else if (a_valid)
        new_seq = a.seq + 1;
    else if (b_valid)
        new_seq = b.seq + 1;
    else
        new_seq = 1;   /* 首次写入 */

    /* 3. 选旧槽写入: 序号较小者(相等或单槽有效时选B, 首次选A) */
    if (a_valid && b_valid)
        use_b = (b.seq <= a.seq) ? 1 : 0;
    else if (a_valid)
        use_b = 1;   /* A 有效则写 B */
    else if (b_valid)
        use_b = 0;   /* B 有效则写 A */
    else
        use_b = 0;   /* 首次写 A */

    /* 4. 填充 g_settings 的 seq/crc/commit */
    g_settings.seq    = new_seq;
    g_settings.crc    = settings_compute_crc(&g_settings);
    g_settings.commit = 0;   /* save_slot 内最后写 commit */

    /* 5. 写入选定的槽 */
    if (use_b)
    {
        if (save_slot(SETTINGS_SLOT_B_ADDR, SETTINGS_SLOT_B_SECTOR) != 0)
        {
            Log_Printf("[SET] save slot B FAILED, trying slot A\r\n");
            /* 失败回退到另一槽 */
            if (save_slot(SETTINGS_SLOT_A_ADDR, SETTINGS_SLOT_A_SECTOR) != 0)
            {
                Log_Printf("[SET] save slot A also FAILED\r\n");
                return -1;
            }
        }
    }
    else
    {
        if (save_slot(SETTINGS_SLOT_A_ADDR, SETTINGS_SLOT_A_SECTOR) != 0)
        {
            Log_Printf("[SET] save slot A FAILED, trying slot B\r\n");
            if (save_slot(SETTINGS_SLOT_B_ADDR, SETTINGS_SLOT_B_SECTOR) != 0)
            {
                Log_Printf("[SET] save slot B also FAILED\r\n");
                return -1;
            }
        }
    }

    Log_Printf("[SET] saved to slot %c, seq=%u\r\n",
               use_b ? 'B' : 'A', (unsigned)new_seq);
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
    /* 同步 DAC 音量，确保 settings 与 music 应用音量一致 */
    DAC_SetVolume((uint8_t)v);
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
    g_settings.seq                = 0;   /* save_to_flash 内会设为 1 */
    g_settings.crc                = 0;   /* save_to_flash 内计算 */
    g_settings.commit             = 0;   /* save_to_flash 内最后写 */
}

/* ---- 快捷访问 ---- */
uint32_t Settings_CursorSensitivity(void) { return g_settings.cursor_sensitivity; }
uint32_t Settings_CursorSize(void)        { return g_settings.cursor_size; }
uint32_t Settings_Brightness(void)        { return g_settings.brightness; }
uint32_t Settings_Volume(void)            { return g_settings.volume; }
uint32_t Settings_ScreenTimeout(void)     { return g_settings.screen_timeout; }
