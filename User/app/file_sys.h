#ifndef FILE_SYS_H
#define FILE_SYS_H

#include <stdint.h>

/* ============================================================
 * 简易块文件系统（基于 SD 卡裸块读写）
 *
 * SD 卡块布局（每块 512 字节）:
 *   Block 0:   文件表（16 个 file_entry_t，共 512 字节）
 *   Block 1:   文件 0 数据（512 字节）
 *   Block 2:   文件 1 数据
 *   ...
 *   Block 16:  文件 15 数据
 *   Block 17:  文件表 CRC 校验块（magic + CRC32，掉电保护）
 *
 * 限制:
 *   - 最多 16 个文件
 *   - 每个文件最大 512 字节
 *   - 文件名 8.3 格式（最多 8 字符名 + 3 字符扩展名）
 *   - 不支持目录
 *
 * 文件表条目 (32 字节):
 *   name[12]:    "FILE01   TXT"（空格填充）
 *   size:        文件大小（字节）
 *   used:        1=已使用, 0=空闲
 *   type:        文件类型 (0=文本, 1=绘图, 2=配置)
 *   block_addr:  存储位置（起始块号）
 *   state:       文件状态 (0=正常, 1=只读, 2=隐藏)
 *   reserved:    填充
 *
 * 存储一致性保护:
 *   save_table 写入文件表后, 计算 Block 0 的 CRC32 并写入 Block 17。
 *   load_table 读取后校验 CRC, 不匹配则告警并清理损坏条目, 避免使用损坏数据。
 * ============================================================ */

#define FS_MAX_FILES        16
#define FS_FILE_MAX_SIZE    512
#define FS_NAME_LEN         12      /* "FILE01   TXT" 含结尾 \0 */
#define FS_BLOCK_TABLE      0       /* 文件表所在块 */
#define FS_BLOCK_DATA_BASE  1       /* 文件数据起始块 */
#define FS_BLOCK_TABLE_CRC  17      /* 文件表 CRC 校验块(掉电保护) */

/* 文件类型 */
#define FS_TYPE_TEXT        0       /* 文本文件 */
#define FS_TYPE_DRAW        1       /* 绘图文件 */
#define FS_TYPE_CONFIG      2       /* 配置文件 */

/* 文件状态 */
#define FS_STATE_NORMAL     0       /* 正常（可读写） */
#define FS_STATE_READONLY   1       /* 只读 */
#define FS_STATE_HIDDEN     2       /* 隐藏 */

typedef struct {
    char     name[FS_NAME_LEN]; /* "FILE01   TXT" 格式 */
    uint32_t size;              /* 文件大小（字节） */
    uint8_t  used;              /* 1=已使用, 0=空闲 */
    uint8_t  type;              /* 文件类型 (FS_TYPE_*) */
    uint16_t block_addr;        /* 存储位置（起始块号） */
    uint8_t  state;             /* 文件状态 (FS_STATE_*) */
    uint8_t  reserved[11];      /* 填充到 32 字节 */
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

/* ============================================================
 * 底层块读写接口（供画图等需要直接存储数据的应用使用）
 * ============================================================ */

/* 读取 SD 卡指定块（512 字节）
 * 返回 0=成功, -1=失败或 SD 卡未就绪 */
int FileSys_ReadRawBlock(uint32_t block, uint8_t *buf);

/* 写入 SD 卡指定块（512 字节）
 * 返回 0=成功, -1=失败或 SD 卡未就绪 */
int FileSys_WriteRawBlock(uint32_t block, const uint8_t *buf);

/* 创建或更新绘图文件条目（type=D）
 * name: 文件名（如 "DRAW.BIN"）
 * block_addr: 绘图数据起始块号
 * size: 绘图数据大小（字节）
 * 返回 >=0: 文件索引, -1: 失败 */
int FileSys_CreateDraw(const char *name, uint16_t block_addr, uint32_t size);

#endif /* FILE_SYS_H */
