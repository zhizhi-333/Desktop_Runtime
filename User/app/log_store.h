#ifndef LOG_STORE_H
#define LOG_STORE_H

#include <stdint.h>

/* ============================================================
 * 日志存储系统
 *
 * 环形缓冲区，最多 LOG_MAX_ENTRIES 条
 * 满了覆盖最旧的
 *
 * 6 类事件（题目要求）：
 *   LOGIN  - 登录失败/成功
 *   FILE   - 文件操作（新建/删除/读写）
 *   SET    - 设置修改
 *   INP    - 输入设备断开/恢复
 *   SCRN   - 熄屏/唤醒
 *   ERR    - 错误事件
 * ============================================================ */

/* 日志类型 */
typedef enum {
    LOG_TYPE_LOGIN,     /* 登录事件 */
    LOG_TYPE_FILE,      /* 文件操作 */
    LOG_TYPE_SETTING,   /* 设置修改 */
    LOG_TYPE_INPUT,     /* 输入设备 */
    LOG_TYPE_SCREEN,    /* 熄屏唤醒 */
    LOG_TYPE_ERROR,     /* 错误事件 */
    LOG_TYPE_INFO,      /* 普通信息（扩展用） */
    LOG_TYPE_COUNT,
} log_type_t;

/* 单条日志 */
typedef struct {
    uint32_t   timestamp;   /* 时间戳（秒，从启动算） */
    log_type_t type;        /* 日志类型 */
    char       text[40];    /* 描述文本（固定长度，便于管理） */
} log_entry_t;

/* 日志最大条数 */
#define LOG_MAX_ENTRIES     32

/* ---- SD 持久化布局 ----
 * 预留专用块区(避开文件表0/文件数据1-16/CRC17/绘图18-57)
 * 4 块 = 2048B, 容纳整个 log_persist_t 结构(约 1.5KB)
 * Block 100: 头(magic+write_idx+count) + entries 前段
 * Block 101~103: entries 剩余部分 */
#define LOG_BLOCK_START     100u
#define LOG_BLOCK_COUNT     4u

/* ---- 初始化 ---- */
void LogStore_Init(void);

/* ---- SD 持久化接口 ----
 * LogStore_Persist:      把 RAM 日志批量写回 SD(由 MonitorTask 周期调用)
 *                        内部 dirty 标志控制, 无变更时立即返回
 * LogStore_LoadFromSD:   启动时从 SD 读取历史日志恢复到 RAM
 *                        首次使用(SD 无有效 magic)时保持 RAM 空状态
 * LogStore_ClearSD:      擦除 SD 上的日志块(配合 LogStore_Clear 使用) */
void LogStore_Persist(void);
void LogStore_LoadFromSD(void);
void LogStore_ClearSD(void);

/* ---- 添加日志（各类专用接口） ---- */

/* 登录事件 */
void LogStore_LoginFail(void);
void LogStore_LoginOK(void);

/* 文件操作：op 描述操作（如 "Create", "Delete", "Read"），name 文件名 */
void LogStore_FileOp(const char *op, const char *name);

/* 设置修改：name 参数名，value 新值 */
void LogStore_SettingChange(const char *name, uint32_t value);

/* 输入设备：device 设备名，connected 0=断开 1=恢复 */
void LogStore_InputEvent(const char *device, int connected);

/* 熄屏唤醒：on 0=熄屏 1=唤醒 */
void LogStore_ScreenEvent(int on);

/* 错误事件：desc 错误描述 */
void LogStore_Error(const char *desc);

/* 普通信息 */
void LogStore_Info(const char *desc);

/* ---- 查询接口（供日志查看应用使用） ---- */

/* 获取日志总数（0 ~ LOG_MAX_ENTRIES） */
int LogStore_GetCount(void);

/* 获取指定索引的日志
 * idx: 0=最新，count-1=最旧
 * 返回：日志条目指针，NULL=索引非法 */
const log_entry_t *LogStore_Get(int idx);

/* 清空所有日志 */
void LogStore_Clear(void);

/* 获取日志类型缩写字符串（如 "LOGIN", "FILE"） */
const char *LogStore_TypeStr(log_type_t type);

#endif /* LOG_STORE_H */
