#ifndef SETTINGS_H
#define SETTINGS_H

#include <stdint.h>

/* ============================================================
 * 系统设置 - 全局参数管理 + Flash 掉电保持
 *
 * 存储位置：双槽冗余
 *   槽A: Flash Sector 10 (0x080C0000)
 *   槽B: Flash Sector 11 (0x080E0000)
 * 存储格式：settings_data_t 结构 + magic + seq + CRC + commit 标记
 *
 * 双槽掉电保护:
 *   保存时写序号较旧的槽: 擦除→写数据+CRC(commit=0)→最后写commit
 *   启动时两槽都读, 选 commit 有效 + CRC 正确 + 序号最新者
 *   掉电发生在写入中时, 旧槽仍可用
 * ============================================================ */

/* 默认值 */
#define SETTINGS_DEFAULT_CURSOR_SENSITIVITY   8       /* 1-20 */
#define SETTINGS_DEFAULT_CURSOR_SIZE          5       /* 3-15 */
#define SETTINGS_DEFAULT_BRIGHTNESS           80      /* 0-100 */
#define SETTINGS_DEFAULT_VOLUME               50      /* 0-100 */
#define SETTINGS_DEFAULT_SCREEN_TIMEOUT       30      /* 5-60 秒 */

/* 范围限制 */
#define SENSITIVITY_MIN   1
#define SENSITIVITY_MAX   20
#define CURSOR_SIZE_MIN   3
#define CURSOR_SIZE_MAX   15
#define BRIGHTNESS_MIN    0
#define BRIGHTNESS_MAX    100
#define VOLUME_MIN        0
#define VOLUME_MAX        100
#define TIMEOUT_MIN       5
#define TIMEOUT_MAX       60

/* 设置数据结构（存入 Flash，4 字节对齐）
 * CRC 字段对前面的所有字段做 CRC32 校验，防止掉电导致数据损坏
 * commit 字段最后单独写入，作为"写入完成"标记 */
typedef struct {
    uint32_t magic;                 /* 魔数，校验数据有效性 */
    uint32_t cursor_sensitivity;    /* 光标灵敏度（1-20） */
    uint32_t cursor_size;           /* 光标大小（3-15） */
    uint32_t brightness;            /* 屏幕亮度（0-100%） */
    uint32_t volume;                /* 系统音量（0-100） */
    uint32_t screen_timeout;        /* 熄屏时间（5-60秒） */
    uint32_t seq;                   /* 序号（每次保存递增，启动选最新者） */
    uint32_t crc;                   /* CRC32(覆盖 magic..seq, 不含本字段和 commit) */
    uint32_t commit;               /* 提交标记(最后写, SETTINGS_COMMITTED=已提交) */
} settings_data_t;

/* 初始化设置（从 Flash 读取，无效则用默认值） */
void Settings_Init(void);

/* 获取当前设置（只读访问） */
const settings_data_t *Settings_Get(void);

/* 设置某项参数（自动限制范围）
 * 返回：0=值变化, 1=值未变化（已是该值） */
int Settings_SetCursorSensitivity(uint32_t v);
int Settings_SetCursorSize(uint32_t v);
int Settings_SetBrightness(uint32_t v);
int Settings_SetVolume(uint32_t v);
int Settings_SetScreenTimeout(uint32_t v);

/* 保存到 Flash（掉电保持）
 * 返回：0=成功, -1=失败 */
int Settings_Save(void);

/* 恢复默认值（不自动保存，需调用 Settings_Save） */
void Settings_ResetDefault(void);

/* ---- 应用层快捷访问 ---- */
uint32_t Settings_CursorSensitivity(void);
uint32_t Settings_CursorSize(void);
uint32_t Settings_Brightness(void);
uint32_t Settings_Volume(void);
uint32_t Settings_ScreenTimeout(void);

#endif /* SETTINGS_H */
