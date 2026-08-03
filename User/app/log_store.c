#include "log_store.h"
#include "monitor.h"
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
 * ============================================================ */

/* ---- 模块状态 ---- */
static log_entry_t entries[LOG_MAX_ENTRIES];
static int write_idx;       /* 下一个写入位置 */
static int count;           /* 当前日志数量 */
static SemaphoreHandle_t log_mutex = NULL;

/* ---- 初始化 ---- */
void LogStore_Init(void)
{
    write_idx = 0;
    count = 0;
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
        return;
    }
    if (xSemaphoreTake(log_mutex, pdMS_TO_TICKS(50)) == pdTRUE)
    {
        count = 0;
        write_idx = 0;
        xSemaphoreGive(log_mutex);
    }
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
