#ifndef FILE_SYS_H
#define FILE_SYS_H

#include <stdint.h>

/* ============================================================
 * 简易块文件系统（基于 SD 卡裸块读写）
 *
 * SD 卡块布局（每块 512 字节）:
 *   Block 0:  文件表（16 个 file_entry_t，共 512 字节）
 *   Block 1:  文件 0 数据（512 字节）
 *   Block 2:  文件 1 数据
 *   ...
 *   Block 16: 文件 15 数据
 *
 * 限制:
 *   - 最多 16 个文件
 *   - 每个文件最大 512 字节
 *   - 文件名 8.3 格式（最多 8 字符名 + 3 字符扩展名）
 *   - 不支持目录
 *
 * 文件表条目 (32 字节):
 *   name[12]:  "FILE01   TXT"（空格填充）
 *   size:      文件大小（字节）
 *   used:      1=已使用, 0=空闲
 *   reserved:  填充
 * ============================================================ */

#define FS_MAX_FILES        16
#define FS_FILE_MAX_SIZE    512
#define FS_NAME_LEN         12      /* "FILE01   TXT" 含结尾 \0 */
#define FS_BLOCK_TABLE      0       /* 文件表所在块 */
#define FS_BLOCK_DATA_BASE  1       /* 文件数据起始块 */

typedef struct {
    char     name[FS_NAME_LEN]; /* "FILE01   TXT" 格式 */
    uint32_t size;              /* 文件大小（字节） */
    uint8_t  used;              /* 1=已使用, 0=空闲 */
    uint8_t  reserved[15];      /* 填充到 32 字节 */
} file_entry_t;  /* sizeof = 32 */

/* 初始化 SD 卡并加载文件表到内存
 * 返回 0=成功, -1=SD 卡不存在或初始化失败 */
int  FileSys_Init(void);

/* SD 卡是否就绪 */
int  FileSys_IsReady(void);

/* 获取已使用的文件数量 */
int  FileSys_GetCount(void);

/* 获取第 idx 个已使用文件的条目（idx 从 0 开始）
 * 返回 NULL 表示越界 */
const file_entry_t *FileSys_GetEntry(int idx);

/* 新建文件
 * name 格式: "FILE01.TXT" 或 "FILE01   TXT"（内部会规范化）
 * 返回 >=0: 新文件索引; -1: 已满; -2: 重名 */
int  FileSys_Create(const char *name);

/* 删除第 idx 个文件
 * 返回 0=成功, -1=越界 */
int  FileSys_Delete(int idx);

/* 重命名第 idx 个文件
 * 返回 0=成功, -1=越界, -2=重名 */
int  FileSys_Rename(int idx, const char *name);

/* 读取第 idx 个文件内容
 * buf:  读缓冲区
 * len:  缓冲区大小
 * 返回: 实际读取的字节数, -1=越界 */
int  FileSys_Read(int idx, uint8_t *buf, int len);

/* 写入第 idx 个文件内容
 * data: 要写入的数据
 * len:  数据长度（不超过 FS_FILE_MAX_SIZE）
 * 返回: 实际写入的字节数, -1=越界或过长 */
int  FileSys_Write(int idx, const uint8_t *data, int len);

/* 检查名字是否已存在
 * 返回 1=已存在, 0=不存在 */
int  FileSys_NameExists(const char *name);

/* 规范化文件名
 * 输入 "FILE01.TXT" → 输出 "FILE01   TXT"（8.3 填充空格） */
void FileSys_FormatName(const char *input, char *output);

/* 把 8.3 格式名转回带点的可读名
 * 输入 "FILE01   TXT" → 输出 "FILE01.TXT" */
void FileSys_PrettyName(const char *input, char *output);

#endif /* FILE_SYS_H */
