#include "log_store.h"
#include "monitor.h"
#include "file_sys.h"
#include "usart.h"
#include "FreeRTOS.h"
#include "task.h"
#include "semphr.h"
#include <string.h>

/* ============================================================
 * 日志存储实现
 *
 * 环形缓冲区：entries[] 数组 + write_idx 写指针 + count 当前数量
 * 新日志写入 write_idx 位置，write_idx 循环递增
 * 读取时：idx=0 对应最新，idx=count-1 对应最旧
 *
 * 同步保护: 互斥量 log_mutex
 *   - 多任务写入(FileTask/MusicTask/MonitorTask/InputTask 都可能写日志)
 *   - SYSMONITOR 应用读取时也需要加锁,避免读到半写状态
 *
 * SD 持久化:
 *   - dirty 标志: 新增日志时置 1
 *   - MonitorTask 每秒调 LogStore_Persist, dirty=0 时跳过
 *   - 持久化结构 log_persist_t 魔数校验 + 直接 memcpy 整个数组
 * ============================================================ */

/* ---- SD 持久化数据结构 ----
 * magic:   "LOG1" 魔数, 校验数据有效性
 * write_idx/count: 环形缓冲指针
 * entries: 整个 RAM 数组直接序列化
 * sizeof ≈ 12 + 32×48 = 1552B, 占 4 个 512B SD 块 */
#define LOG_PERSIST_MAGIC  0x4C4F4731u   /* 'L''O''G''1' */
typedef struct {
    uint32_t     magic;
    int          write_idx;
    int          count;
    log_entry_t  entries[LOG_MAX_ENTRIES];
} log_persist_t;

/* ---- 模块状态 ---- */
static log_entry_t entries[LOG_MAX_ENTRIES];
static int write_idx;       /* 下一个写入位置 */
static int count;           /* 当前日志数量 */
static int dirty;           /* 1=有未刷盘的新日志, 0=与 SD 一致 */
static SemaphoreHandle_t log_mutex = NULL;

/* ---- 初始化 ---- */
void LogStore_Init(void)
{
    write_idx = 0;
    count = 0;
    dirty = 0;
    memset(entries, 0, sizeof(entries));
    /* 调度器启动前创建 mutex(在 main.c 的 LogStore_Init 中调用) */
    log_mutex = xSemaphoreCreateMutex();
}

/* ---- 内部：添加一条日志(调用方已持锁) ---- */
static void add_entry_locked(log_type_t type, const char *text)
{
    log_entry_t *e = &entries[write_idx];

    /* 时间戳（秒） */
    e->timestamp = (uint32_t)(xTaskGetTickCount() / 1000);
    e->type = type;

    /* 复制文本（截断保护） */
    strncpy(e->text, text, sizeof(e->text) - 1);
    e->text[sizeof(e->text) - 1] = 0;

    /* 环形递增 */
    write_idx = (write_idx + 1) % LOG_MAX_ENTRIES;
    if (count < LOG_MAX_ENTRIES) count++;

    dirty = 1;   /* 标记需要刷盘 */
}

/* ---- 内部：添加一条日志(加锁版,供外部接口调用) ---- */
static void add_entry(log_type_t type, const char *text)
{
    /* 调度器未启动时直接写(初始化阶段,单任务) */
    if (xTaskGetSchedulerState() == taskSCHEDULER_NOT_STARTED || log_mutex == NULL)
    {
        add_entry_locked(type, text);
        return;
    }
    if (xSemaphoreTake(log_mutex, pdMS_TO_TICKS(50)) == pdTRUE)
    {
        add_entry_locked(type, text);
        xSemaphoreGive(log_mutex);
    }
}

/* ---- 各类专用接口 ---- */

void LogStore_LoginFail(void)
{
    add_entry(LOG_TYPE_LOGIN, "Login failed");
}

void LogStore_LoginOK(void)
{
    add_entry(LOG_TYPE_LOGIN, "Login OK");
}

void LogStore_FileOp(const char *op, const char *name)
{
    char buf[40];
    /* 拼接 "op name"，防止溢出 */
    int i = 0;
    if (op) {
        while (op[i] && i < 20) { buf[i] = op[i]; i++; }
        if (i < 39) buf[i++] = ' ';
    }
    if (name) {
        while (name[i - (op ? (int)strlen(op) + 1 : 0)] && i < 39) {
            buf[i] = name[i - (op ? (int)strlen(op) + 1 : 0)];
            i++;
        }
    }
    buf[i] = 0;
    add_entry(LOG_TYPE_FILE, buf);
}

void LogStore_SettingChange(const char *name, uint32_t value)
{
    char buf[40];
    int i = 0;
    if (name) {
        while (name[i] && i < 20) { buf[i] = name[i]; i++; }
    }
    buf[i++] = '=';
    /* 数字转字符串 */
    {
        char num[12];
        int n = 0;
        uint32_t v = value;
        if (v == 0) { num[n++] = '0'; }
        while (v > 0 && n < 11) { num[n++] = '0' + (v % 10); v /= 10; }
        while (n > 0 && i < 39) { buf[i++] = num[--n]; }
    }
    buf[i] = 0;
    add_entry(LOG_TYPE_SETTING, buf);
}

void LogStore_InputEvent(const char *device, int connected)
{
    char buf[40];
    int i = 0;
    if (device) {
        while (device[i] && i < 30) { buf[i] = device[i]; i++; }
    }
    if (connected) {
        const char *s = " restored";
        while (*s && i < 39) buf[i++] = *s++;
    } else {
        const char *s = " disconnected";
        while (*s && i < 39) buf[i++] = *s++;
    }
    buf[i] = 0;
    add_entry(LOG_TYPE_INPUT, buf);
}

void LogStore_ScreenEvent(int on)
{
    add_entry(LOG_TYPE_SCREEN, on ? "Backlight ON" : "Backlight OFF");
}

void LogStore_Error(const char *desc)
{
    add_entry(LOG_TYPE_ERROR, desc ? desc : "Unknown error");
    Monitor_IncError();
}

void LogStore_Info(const char *desc)
{
    add_entry(LOG_TYPE_INFO, desc ? desc : "");
}

/* ---- 查询接口(加锁保护) ---- */
int LogStore_GetCount(void)
{
    int c;
    if (xTaskGetSchedulerState() == taskSCHEDULER_NOT_STARTED || log_mutex == NULL)
        return count;
    if (xSemaphoreTake(log_mutex, pdMS_TO_TICKS(50)) == pdTRUE)
    {
        c = count;
        xSemaphoreGive(log_mutex);
        return c;
    }
    return count;
}

const log_entry_t *LogStore_Get(int idx)
{
    int real_idx;
    int c;
    const log_entry_t *ret = NULL;

    if (xTaskGetSchedulerState() == taskSCHEDULER_NOT_STARTED || log_mutex == NULL)
    {
        if (idx < 0 || idx >= count) return NULL;
        real_idx = (write_idx - 1 - idx + LOG_MAX_ENTRIES) % LOG_MAX_ENTRIES;
        return &entries[real_idx];
    }

    if (xSemaphoreTake(log_mutex, pdMS_TO_TICKS(50)) == pdTRUE)
    {
        c = count;
        if (idx >= 0 && idx < c)
        {
            real_idx = (write_idx - 1 - idx + LOG_MAX_ENTRIES) % LOG_MAX_ENTRIES;
            ret = &entries[real_idx];
        }
        xSemaphoreGive(log_mutex);
    }
    return ret;
}

void LogStore_Clear(void)
{
    if (xTaskGetSchedulerState() == taskSCHEDULER_NOT_STARTED || log_mutex == NULL)
    {
        count = 0;
        write_idx = 0;
        dirty = 0;
        return;
    }
    if (xSemaphoreTake(log_mutex, pdMS_TO_TICKS(50)) == pdTRUE)
    {
        count = 0;
        write_idx = 0;
        dirty = 0;   /* RAM 已空, 不需要刷盘 */
        xSemaphoreGive(log_mutex);
    }

    /* 同步擦除 SD 上的日志块, 保证 RAM 与 SD 一致 */
    LogStore_ClearSD();
}

const char *LogStore_TypeStr(log_type_t type)
{
    switch (type)
    {
        case LOG_TYPE_LOGIN:    return "LOGIN";
        case LOG_TYPE_FILE:     return "FILE ";
        case LOG_TYPE_SETTING:  return "SET  ";
        case LOG_TYPE_INPUT:    return "INP  ";
        case LOG_TYPE_SCREEN:   return "SCRN ";
        case LOG_TYPE_ERROR:    return "ERR  ";
        case LOG_TYPE_INFO:     return "INFO ";
        default:                return "?????";
    }
}

/* ============================================================
 * SD 持久化实现
 *
 * LogStore_Persist:  MonitorTask 每秒调用, dirty 触发刷盘
 *                   把整个 RAM 数组序列化到 SD Block 100~103
 * LogStore_LoadFromSD: 启动时从 SD 读取恢复 RAM 状态
 * LogStore_ClearSD:  清空 SD 上的日志块
 *
 * 同步关系: 通过 FileSys_ReadRawBlock/WriteRawBlock 间接使用
 *          file_sys 内的 fs_sd_mutex, 与 FileTask 互斥访问 SD
 * ============================================================ */

void LogStore_Persist(void)
{
    /* 静态缓冲, 避免占用任务栈(2KB, MonitorTask 栈 1KB 字不够) */
    static log_persist_t persist_buf;
    const uint8_t *p = (const uint8_t *)&persist_buf;
    int total = (int)sizeof(log_persist_t);
    int blocks = (total + 511) / 512;
    int i;

    if (!dirty) return;
    if (blocks > (int)LOG_BLOCK_COUNT) blocks = (int)LOG_BLOCK_COUNT;

    /* 1. 锁定 log_mutex 复制 RAM 状态到持久化缓冲 */
    if (xTaskGetSchedulerState() == taskSCHEDULER_NOT_STARTED || log_mutex == NULL)
    {
        /* 调度器未启动时不刷盘(初始化阶段无并发也不需要) */
        return;
    }
    if (xSemaphoreTake(log_mutex, pdMS_TO_TICKS(50)) != pdTRUE)
        return;   /* 锁失败, 下次再试 */

    persist_buf.magic     = LOG_PERSIST_MAGIC;
    persist_buf.write_idx = write_idx;
    persist_buf.count     = count;
    memcpy(persist_buf.entries, entries, sizeof(entries));
    xSemaphoreGive(log_mutex);

    /* 2. 分块写入 SD (FileSys_WriteRawBlock 内部有 fs_sd_mutex 保护) */
    for (i = 0; i < blocks; i++)
    {
        uint8_t blk[512];
        int off = i * 512;
        int copy_len = total - off;
        if (copy_len > 512) copy_len = 512;

        memset(blk, 0, sizeof(blk));
        if (copy_len > 0) memcpy(blk, p + off, copy_len);

        if (FileSys_WriteRawBlock(LOG_BLOCK_START + i, blk) != 0)
        {
            Log_Printf("[LOG] persist write block %d failed\r\n", i);
            return;   /* 写失败, dirty 保持 1, 下次重试 */
        }
    }

    dirty = 0;
    Log_Printf("[LOG] persisted %d entries to SD\r\n", count);
}

void LogStore_LoadFromSD(void)
{
    static log_persist_t persist_buf;   /* 静态缓冲, 避免栈溢出 */
    uint8_t *p = (uint8_t *)&persist_buf;
    int total = (int)sizeof(log_persist_t);
    int blocks = (total + 511) / 512;
    int i;

    if (blocks > (int)LOG_BLOCK_COUNT) blocks = (int)LOG_BLOCK_COUNT;

    /* 1. 分块读取 SD */
    for (i = 0; i < blocks; i++)
    {
        uint8_t blk[512];
        int off = i * 512;
        int copy_len = total - off;
        if (copy_len > 512) copy_len = 512;

        if (FileSys_ReadRawBlock(LOG_BLOCK_START + i, blk) != 0)
        {
            Log_Printf("[LOG] load block %d failed, keep empty\r\n", i);
            return;   /* 读取失败, 保持 RAM 空状态 */
        }
        if (copy_len > 0) memcpy(p + off, blk, copy_len);
    }

    /* 2. 校验 magic (首次使用 SD 无有效数据) */
    if (persist_buf.magic != LOG_PERSIST_MAGIC)
    {
        Log_Printf("[LOG] no persisted data (magic mismatch), fresh start\r\n");
        return;
    }

    /* 3. 校验指针合法性 */
    if (persist_buf.count < 0 || persist_buf.count > LOG_MAX_ENTRIES)
    {
        Log_Printf("[LOG] persisted count=%d invalid, fresh start\r\n",
                   persist_buf.count);
        return;
    }
    if (persist_buf.write_idx < 0 || persist_buf.write_idx >= LOG_MAX_ENTRIES)
    {
        Log_Printf("[LOG] persisted write_idx=%d invalid, fresh start\r\n",
                   persist_buf.write_idx);
        return;
    }

    /* 4. 恢复 RAM 状态 */
    if (xSemaphoreTake(log_mutex, pdMS_TO_TICKS(50)) == pdTRUE)
    {
        write_idx = persist_buf.write_idx;
        count     = persist_buf.count;
        memcpy(entries, persist_buf.entries, sizeof(entries));
        dirty     = 0;   /* RAM 与 SD 一致, 无需立即刷盘 */
        xSemaphoreGive(log_mutex);
    }

    Log_Printf("[LOG] loaded %d entries from SD\r\n", count);
}

void LogStore_ClearSD(void)
{
    uint8_t zero[512];
    int i;

    memset(zero, 0, sizeof(zero));
    for (i = 0; i < (int)LOG_BLOCK_COUNT; i++)
    {
        FileSys_WriteRawBlock(LOG_BLOCK_START + (uint32_t)i, zero);
    }
    Log_Printf("[LOG] SD log blocks cleared\r\n");
}
