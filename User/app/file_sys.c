#include "file_sys.h"
#include "stm32f4xx_hal.h"
#include <string.h>
#include <stdio.h>

/* ============================================================
 * 简易块文件系统实现
 *
 * 文件表缓存在内存中，修改后调用 save_table() 写回 SD 卡
 * 文件数据直接读写 SD 卡对应块
 * ============================================================ */

/* ---- SD 卡句柄（模块内部） ---- */
static SD_HandleTypeDef hsd;
static int sd_ready;                /* SD 卡是否就绪 */

/* ---- 文件表缓存（内存中） ---- */
static file_entry_t table[FS_MAX_FILES];

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
    if (!sd_ready) return -1;
    if (HAL_SD_ReadBlocks(&hsd, buf, block, 1, 2000) != HAL_OK)
        return -1;
    /* 等待传输完成 */
    while (HAL_SD_GetCardState(&hsd) != HAL_SD_CARD_TRANSFER)
        ;
    return 0;
}

/* ---- 写一个块（512 字节） ---- */
/* 注: HAL_SD_WriteBlocks 要求可写指针，用临时缓冲避免 const 丢失 */
static int write_block(uint32_t block, const uint8_t *buf)
{
    uint8_t tmp[512];
    if (!sd_ready) return -1;
    memcpy(tmp, buf, 512);
    if (HAL_SD_WriteBlocks(&hsd, tmp, block, 1, 2000) != HAL_OK)
        return -1;
    while (HAL_SD_GetCardState(&hsd) != HAL_SD_CARD_TRANSFER)
        ;
    return 0;
}

/* ---- 把内存中的文件表写回 SD 卡 ---- */
static int save_table(void)
{
    uint8_t buf[512];

    memset(buf, 0, sizeof(buf));
    memcpy(buf, table, sizeof(table));
    return write_block(FS_BLOCK_TABLE, buf);
}

/* ---- 从 SD 卡加载文件表到内存 ---- */
static int load_table(void)
{
    uint8_t buf[512];

    if (read_block(FS_BLOCK_TABLE, buf) != 0)
        return -1;
    memcpy(table, buf, sizeof(table));
    return 0;
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
