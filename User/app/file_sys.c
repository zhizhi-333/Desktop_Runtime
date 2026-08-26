#include "file_sys.h"
#include "stm32f4xx_hal.h"
#include "usart.h"
#include "FreeRTOS.h"
#include "semphr.h"
#include <string.h>
#include <stdio.h>

/* ============================================================
 * 简易块文件系统实现
 *
 * 文件表缓存在内存中，修改后调用 save_table() 写回 SD 卡
 * 文件数据直接读写 SD 卡对应块
 *
 * 存储一致性保护:
 *   save_table 写入文件表(Block 0)后, 计算 CRC32 并写入 Block 17
 *   load_table 读取后校验 CRC, 不匹配则告警并清理损坏条目
 * ============================================================ */

/* 文件表 CRC 校验块魔数 "FST1" */
#define FS_TABLE_MAGIC  0x46535431u

/* ---- SD 卡句柄（模块内部） ---- */
static SD_HandleTypeDef hsd;
static int sd_ready;                /* SD 卡是否就绪 */

/* ---- 文件表缓存（内存中） ---- */
static file_entry_t table[FS_MAX_FILES];

/* ---- 文件表 CRC 损坏标志(load_table 检测到, FileSys_Init 据此重写) ---- */
static int table_crc_corrupted = 0;

/* ---- SD 块读写互斥量
 * 保护 FileTask 与 MonitorTask(日志持久化) 的并发 SD 访问
 * FreeRTOS Mutex 带优先级继承,避免死锁 ---- */
static SemaphoreHandle_t fs_sd_mutex = NULL;

/* 内部:获取/释放 SD 锁, mutex 为 NULL 时(初始化前)直接放行 */
static void fs_sd_lock(void)
{
    if (fs_sd_mutex != NULL)
        xSemaphoreTake(fs_sd_mutex, portMAX_DELAY);
}
static void fs_sd_unlock(void)
{
    if (fs_sd_mutex != NULL)
        xSemaphoreGive(fs_sd_mutex);
}

/* ---- 底层块读写前向声明(供 CRC 校验函数使用) ---- */
static int read_block(uint32_t block, uint8_t *buf);
static int write_block(uint32_t block, const uint8_t *buf);

/* ---- CRC32 (多项式 0xEDB88320, 与 zlib/zip 一致) ----
 * 用于校验文件表数据完整性, 防止掉电写入不完整导致文件表损坏 */
static uint32_t fs_crc32(const uint8_t *data, int len)
{
    uint32_t crc = 0xFFFFFFFFu;
    int i;
    for (i = 0; i < len; i++)
    {
        crc ^= data[i];
        int b;
        for (b = 0; b < 8; b++)
        {
            if (crc & 1u)
                crc = (crc >> 1) ^ 0xEDB88320u;
            else
                crc = (crc >> 1);
        }
    }
    return ~crc;
}

/* ---- 写入文件表 CRC 校验块到 Block 17 ---- */
static int save_table_crc(uint32_t crc)
{
    uint8_t buf[512];
    uint32_t *p = (uint32_t *)buf;

    memset(buf, 0, sizeof(buf));
    p[0] = FS_TABLE_MAGIC;
    p[1] = crc;
    return write_block(FS_BLOCK_TABLE_CRC, buf);
}

/* ---- 读取并校验文件表 CRC, 返回 0=匹配, 1=无CRC(首次), -1=不匹配 ---- */
static int verify_table_crc(const uint8_t *table_buf)
{
    uint8_t buf[512];
    uint32_t *p = (uint32_t *)buf;
    uint32_t computed;

    if (read_block(FS_BLOCK_TABLE_CRC, buf) != 0)
        return 1;  /* 读取失败(首次使用), 视为无 CRC */

    if (p[0] != FS_TABLE_MAGIC)
        return 1;  /* 无魔数(首次使用), 视为无 CRC */

    computed = fs_crc32(table_buf, 512);
    return (p[1] == computed) ? 0 : -1;
}

/* ---- SDIO GPIO 初始化 ---- */
static void sdio_gpio_init(void)
{
    GPIO_InitTypeDef gpio = {0};

    __HAL_RCC_GPIOC_CLK_ENABLE();
    __HAL_RCC_GPIOD_CLK_ENABLE();
    __HAL_RCC_SDIO_CLK_ENABLE();

    gpio.Mode = GPIO_MODE_AF_PP;
    gpio.Pull = GPIO_PULLUP;
    gpio.Speed = GPIO_SPEED_FREQ_HIGH;
    gpio.Alternate = GPIO_AF12_SDIO;

    /* PC8~PC12: SDIO D0~D3, CLK */
    gpio.Pin = GPIO_PIN_8 | GPIO_PIN_9 | GPIO_PIN_10 | GPIO_PIN_11 | GPIO_PIN_12;
    HAL_GPIO_Init(GPIOC, &gpio);

    /* PD2: SDIO CMD */
    gpio.Pin = GPIO_PIN_2;
    HAL_GPIO_Init(GPIOD, &gpio);
}

/* ---- 读一个块（512 字节） ---- */
static int read_block(uint32_t block, uint8_t *buf)
{
    int ret;
    if (!sd_ready) return -1;
    fs_sd_lock();
    if (HAL_SD_ReadBlocks(&hsd, buf, block, 1, 2000) != HAL_OK)
    {
        fs_sd_unlock();
        return -1;
    }
    /* 等待传输完成 */
    while (HAL_SD_GetCardState(&hsd) != HAL_SD_CARD_TRANSFER)
        ;
    ret = 0;
    fs_sd_unlock();
    return ret;
}

/* ---- 写一个块（512 字节） ---- */
/* 注: HAL_SD_WriteBlocks 要求可写指针，用临时缓冲避免 const 丢失 */
static int write_block(uint32_t block, const uint8_t *buf)
{
    uint8_t tmp[512];
    int ret;
    if (!sd_ready) return -1;
    memcpy(tmp, buf, 512);
    fs_sd_lock();
    if (HAL_SD_WriteBlocks(&hsd, tmp, block, 1, 2000) != HAL_OK)
    {
        fs_sd_unlock();
        return -1;
    }
    while (HAL_SD_GetCardState(&hsd) != HAL_SD_CARD_TRANSFER)
        ;
    ret = 0;
    fs_sd_unlock();
    return ret;
}

/* ---- 底层块读写公共接口（供画图等应用直接存储数据） ---- */
int FileSys_ReadRawBlock(uint32_t block, uint8_t *buf)
{
    return read_block(block, buf);
}

/* 前向声明: save_table 定义在后面，CreateDraw 需要先调用 */
static int save_table(void);

int FileSys_WriteRawBlock(uint32_t block, const uint8_t *buf)
{
    return write_block(block, buf);
}

/* ---- 创建或更新绘图文件条目 ---- */
int FileSys_CreateDraw(const char *name, uint16_t block_addr, uint32_t size)
{
    char formatted[FS_NAME_LEN];
    int i;

    FileSys_FormatName(name, formatted);

    /* 先查找是否已存在同名绘图文件 */
    for (i = 0; i < FS_MAX_FILES; i++)
    {
        if (table[i].used && memcmp(table[i].name, formatted, 11) == 0)
        {
            /* 已存在: 更新 size 和 block_addr */
            table[i].size       = size;
            table[i].block_addr = block_addr;
            table[i].type       = FS_TYPE_DRAW;
            table[i].state      = FS_STATE_NORMAL;
            save_table();
            return i;
        }
    }

    /* 不存在: 找空闲槽位创建 */
    for (i = 0; i < FS_MAX_FILES; i++)
    {
        if (!table[i].used)
        {
            memset(&table[i], 0, sizeof(file_entry_t));
            memcpy(table[i].name, formatted, 11);
            table[i].size       = size;
            table[i].used       = 1;
            table[i].type       = FS_TYPE_DRAW;
            table[i].block_addr = block_addr;
            table[i].state      = FS_STATE_NORMAL;
            save_table();
            return i;
        }
    }

    return -1;  /* 已满 */
}

/* ---- 把内存中的文件表写回 SD 卡(双块原子保存) ----
 * 写入顺序: 主块(0)→CRC块(17)→备份块(58)
 *
 * 掉电场景分析:
 *   写主块中:   主块=损坏, CRC=旧, 备份块=旧 → 主块CRC不匹配 → 备份块+旧CRC匹配 → 恢复 ✅
 *   写CRC前:    主块=新, CRC=旧 → 主块CRC不匹配 → 备份块=旧+旧CRC匹配 → 恢复(旧数据) ✅
 *   写CRC后/备份前: 主块=新, CRC=新, 备份块=旧 → 主块CRC匹配 → 用主块 ✅
 *   写备份块中: 主块=新, CRC=新 → 主块CRC匹配 → 用主块(备份损坏不影响) ✅
 */
static int save_table(void)
{
    uint8_t buf[512];
    uint32_t crc;

    memset(buf, 0, sizeof(buf));
    memcpy(buf, table, sizeof(table));
    crc = fs_crc32(buf, 512);

    /* 1. 写主块(Block 0): 文件表实际读取位置 */
    if (write_block(FS_BLOCK_TABLE, buf) != 0)
        return -1;

    /* 2. 写 CRC 校验块(Block 17): 主块完整性标记
     * CRC 块对应"最后一次成功写入的主块" */
    save_table_crc(crc);

    /* 3. 写备份块(Block 58): 主块的副本, 主块损坏时恢复用
     * 备份块内容=主块, CRC 块同时校验两者(它们内容相同) */
    if (write_block(FS_BLOCK_TABLE_BAK, buf) != 0)
    {
        Log_Printf("[FS] backup block write FAILED\r\n");
        /* 备份失败不阻塞主流程, 主块已写成功 */
    }

    return 0;
}

/* ---- 从 SD 卡加载文件表到内存(双块恢复) ----
 * 读主块+CRC校验, 失败则读备份块+CRC校验恢复
 * 返回 0=成功, -1=读取失败 */
static int load_table(void)
{
    uint8_t buf[512];
    uint8_t bak_buf[512];

    /* 1. 先读主块 */
    if (read_block(FS_BLOCK_TABLE, buf) != 0)
        return -1;

    /* 2. 校验主块 CRC */
    {
        int crc_ret = verify_table_crc(buf);
        if (crc_ret == 0)
        {
            /* CRC 匹配: 主块数据完整 */
            memcpy(table, buf, sizeof(table));
            return 0;
        }
        else if (crc_ret == 1)
        {
            /* 无 CRC(首次使用): 正常加载, 后续 save_table 会建立 CRC 和备份 */
            memcpy(table, buf, sizeof(table));
            Log_Printf("[FS] table CRC not present (first use), will create\r\n");
            return 0;
        }
        else
        {
            /* CRC 不匹配: 主块可能损坏, 尝试从备份块恢复 */
            Log_Printf("[FS] !!! table CRC mismatch! trying backup block !!!\r\n");

            if (read_block(FS_BLOCK_TABLE_BAK, bak_buf) == 0)
            {
                /* 备份块读成功, 用同一个 CRC 块校验
                 * (备份块内容=主块, CRC 块同时校验两者) */
                uint32_t bak_crc = fs_crc32(bak_buf, 512);
                uint8_t crc_blk[512];
                uint32_t *p = (uint32_t *)crc_blk;

                if (read_block(FS_BLOCK_TABLE_CRC, crc_blk) == 0
                    && p[0] == FS_TABLE_MAGIC
                    && p[1] == bak_crc)
                {
                    /* 备份块 CRC 匹配: 从备份块恢复 */
                    memcpy(table, bak_buf, sizeof(table));
                    Log_Printf("[FS] recovered table from backup block\r\n");
                    /* 立即重写主块, 让下次启动能直接用主块 */
                    save_table();
                    return 0;
                }
            }

            /* 备份块也损坏或不存在: 加载主块数据(可能有损坏), 由 check_table_init 清理 */
            memcpy(table, buf, sizeof(table));
            table_crc_corrupted = 1;
            Log_Printf("[FS] !!! backup also corrupted, sanitizing !!!\r\n");
            return 0;
        }
    }
}

/* ---- 检查文件表是否需要初始化 ---- */
static void check_table_init(void)
{
    int i;
    int has_valid = 0;

    /* 检查是否有任何 used 标志为 1 的条目
     * 如果全是 0 或 0xFF，说明是全新卡，需要初始化 */
    for (i = 0; i < FS_MAX_FILES; i++)
    {
        if (table[i].used == 1)
        {
            has_valid = 1;
            break;
        }
    }

    /* 检查是否有全 0xFF（未擦除的新卡） */
    if (!has_valid)
    {
        /* 可能是全新 SD 卡，数据为 0xFF，初始化为空表 */
        for (i = 0; i < FS_MAX_FILES; i++)
        {
            if (table[i].used != 0 && table[i].used != 1)
            {
                /* 数据异常，初始化 */
                memset(table, 0, sizeof(table));
                save_table();
                return;
            }
        }
    }
}

/* ============================================================
 * 公共接口实现
 * ============================================================ */

int FileSys_Init(void)
{
    /* 防止重复初始化（main.c 启动时已调用，app_file.c 不应再调） */
    static int fs_inited = 0;
    if (fs_inited)
        return sd_ready ? 0 : -1;
    fs_inited = 1;

    /* 创建 SD 互斥量(供 FileTask 与 LogStore 持久化并发使用) */
    if (fs_sd_mutex == NULL)
        fs_sd_mutex = xSemaphoreCreateMutex();

    sdio_gpio_init();

    HAL_NVIC_SetPriority(SDIO_IRQn, 0x0E, 0);
    HAL_NVIC_EnableIRQ(SDIO_IRQn);

    hsd.Instance = SDIO;
    hsd.Init.ClockEdge = SDIO_CLOCK_EDGE_RISING;
    hsd.Init.ClockBypass = SDIO_CLOCK_BYPASS_DISABLE;
    hsd.Init.ClockPowerSave = SDIO_CLOCK_POWER_SAVE_DISABLE;
    hsd.Init.BusWide = SDIO_BUS_WIDE_1B;
    hsd.Init.HardwareFlowControl = SDIO_HARDWARE_FLOW_CONTROL_DISABLE;
    hsd.Init.ClockDiv = 0;

    if (HAL_SD_Init(&hsd) != HAL_OK)
    {
        sd_ready = 0;
        return -1;
    }

    sd_ready = 1;

    /* 加载文件表 */
    if (load_table() != 0)
    {
        /* 读取失败，初始化空表 */
        memset(table, 0, sizeof(table));
        save_table();
    }
    else
    {
        check_table_init();
        /* CRC 损坏后: 清理完异常条目, 重写文件表 + 新 CRC, 避免持续使用损坏数据 */
        if (table_crc_corrupted)
        {
            save_table();
            table_crc_corrupted = 0;
            Log_Printf("[FS] table re-saved with fresh CRC after corruption\r\n");
        }
    }

    return 0;
}

int FileSys_IsReady(void)
{
    return sd_ready;
}

int FileSys_GetCount(void)
{
    int i, count = 0;
    for (i = 0; i < FS_MAX_FILES; i++)
    {
        if (table[i].used) count++;
    }
    return count;
}

const file_entry_t *FileSys_GetEntry(int idx)
{
    int i, count = 0;
    for (i = 0; i < FS_MAX_FILES; i++)
    {
        if (table[i].used)
        {
            if (count == idx) return &table[i];
            count++;
        }
    }
    return NULL;
}

/* ---- 规范化文件名: "FILE01.TXT" → "FILE01   TXT" ---- */
void FileSys_FormatName(const char *input, char *output)
{
    char name[9] = {0};
    char ext[4] = {0};
    const char *dot;
    int i;

    dot = strchr(input, '.');
    if (dot)
    {
        int name_len = dot - input;
        int ext_len = strlen(dot + 1);
        if (name_len > 8) name_len = 8;
        if (ext_len > 3) ext_len = 3;
        memcpy(name, input, name_len);
        memcpy(ext, dot + 1, ext_len);
    }
    else
    {
        int name_len = strlen(input);
        if (name_len > 8) name_len = 8;
        memcpy(name, input, name_len);
    }

    /* 填充空格到 8 字符 */
    for (i = 0; i < 8; i++)
    {
        if (name[i] == 0) name[i] = ' ';
    }

    /* 组合: 8字符名 + 3字符扩展名 */
    memcpy(output, name, 8);
    memcpy(output + 8, ext, 3);
    output[11] = 0;
}

/* ---- 可读名: "FILE01   TXT" → "FILE01.TXT" ---- */
void FileSys_PrettyName(const char *input, char *output)
{
    char name[9] = {0};
    char ext[4] = {0};
    int i, name_end = 7;

    memcpy(name, input, 8);
    name[8] = 0;
    memcpy(ext, input + 8, 3);
    ext[3] = 0;

    /* 去掉名部末尾空格 */
    for (i = 7; i >= 0; i--)
    {
        if (name[i] != ' ')
        {
            name_end = i;
            break;
        }
        name[i] = 0;
    }

    /* 如果有扩展名 */
    if (ext[0] != 0 && ext[0] != ' ')
    {
        /* 去掉扩展名末尾空格 */
        for (i = 2; i >= 0; i--)
        {
            if (ext[i] == ' ') ext[i] = 0;
            else break;
        }
        snprintf(output, FS_NAME_LEN, "%s.%s", name, ext);
    }
    else
    {
        strcpy(output, name);
    }
}

int FileSys_NameExists(const char *name)
{
    char formatted[FS_NAME_LEN];
    int i;

    FileSys_FormatName(name, formatted);

    for (i = 0; i < FS_MAX_FILES; i++)
    {
        if (table[i].used && memcmp(table[i].name, formatted, 11) == 0)
            return 1;
    }
    return 0;
}

int FileSys_Create(const char *name)
{
    char formatted[FS_NAME_LEN];
    int i;

    if (FileSys_NameExists(name))
        return -2;  /* 重名 */

    /* 找一个空闲槽位 */
    for (i = 0; i < FS_MAX_FILES; i++)
    {
        if (!table[i].used)
        {
            FileSys_FormatName(name, formatted);
            memset(&table[i], 0, sizeof(file_entry_t));
            memcpy(table[i].name, formatted, 11);
            table[i].size = 0;
            table[i].used = 1;
            table[i].type = FS_TYPE_TEXT;              /* 默认文本类型 */
            table[i].block_addr = FS_BLOCK_DATA_BASE + i;  /* 存储位置 */
            table[i].state = FS_STATE_NORMAL;          /* 正常状态 */

            if (save_table() != 0)
                return -1;
            return i;
        }
    }

    return -1;  /* 已满 */
}

int FileSys_Delete(int idx)
{
    int i, count = 0;
    uint8_t zero_buf[512];

    for (i = 0; i < FS_MAX_FILES; i++)
    {
        if (table[i].used)
        {
            if (count == idx)
            {
                /* 清除文件表条目 */
                table[i].used = 0;
                table[i].size = 0;
                memset(table[i].name, 0, FS_NAME_LEN);
                save_table();

                /* 清除文件数据块 */
                memset(zero_buf, 0, sizeof(zero_buf));
                write_block(FS_BLOCK_DATA_BASE + i, zero_buf);
                return 0;
            }
            count++;
        }
    }
    return -1;  /* 越界 */
}

int FileSys_Rename(int idx, const char *name)
{
    int i, count = 0;
    char formatted[FS_NAME_LEN];

    FileSys_FormatName(name, formatted);

    for (i = 0; i < FS_MAX_FILES; i++)
    {
        if (table[i].used)
        {
            if (count == idx)
            {
                /* 检查重名（排除自身） */
                int j, cnt2 = 0;
                for (j = 0; j < FS_MAX_FILES; j++)
                {
                    if (table[j].used)
                    {
                        if (cnt2 != idx && memcmp(table[j].name, formatted, 11) == 0)
                            return -2;  /* 重名 */
                        cnt2++;
                    }
                }

                /* 重命名 */
                memset(table[i].name, 0, FS_NAME_LEN);
                memcpy(table[i].name, formatted, 11);
                save_table();
                return 0;
            }
            count++;
        }
    }
    return -1;  /* 越界 */
}

int FileSys_Read(int idx, uint8_t *buf, int len)
{
    int i, count = 0;
    uint8_t block_buf[512];

    for (i = 0; i < FS_MAX_FILES; i++)
    {
        if (table[i].used)
        {
            if (count == idx)
            {
                int read_len = table[i].size;
                if (read_len > len) read_len = len;
                if (read_len > FS_FILE_MAX_SIZE) read_len = FS_FILE_MAX_SIZE;

                if (sd_ready)
                {
                    if (read_block(FS_BLOCK_DATA_BASE + i, block_buf) != 0)
                        return -1;
                    memcpy(buf, block_buf, read_len);
                }
                else
                {
                    memset(buf, 0, read_len);
                }
                return read_len;
            }
            count++;
        }
    }
    return -1;
}

int FileSys_Write(int idx, const uint8_t *data, int len)
{
    int i, count = 0;
    uint8_t block_buf[512];

    if (len > FS_FILE_MAX_SIZE) return -1;

    for (i = 0; i < FS_MAX_FILES; i++)
    {
        if (table[i].used)
        {
            if (count == idx)
            {
                /* 准备数据块（不足部分填 0） */
                memset(block_buf, 0, sizeof(block_buf));
                memcpy(block_buf, data, len);

                /* 写入数据块 */
                if (sd_ready)
                {
                    if (write_block(FS_BLOCK_DATA_BASE + i, block_buf) != 0)
                        return -1;
                }

                /* 更新文件大小 */
                table[i].size = len;
                save_table();
                return len;
            }
            count++;
        }
    }
    return -1;
}
