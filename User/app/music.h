#ifndef MUSIC_H
#define MUSIC_H

#include <stdint.h>

/* ============================================================
 * 音乐播放引擎
 *
 * 音符表: 覆盖 3 个八度 (C3~C6)
 * 旋律: 音符索引 + 时值(节拍数)
 *
 * 使用:
 *   1. Music_Init()           初始化
 *   2. Music_Load(track)      加载曲目
 *   3. Music_Play()           开始播放
 *   4. Music_Update()         每帧调用,推进播放
 *   5. Music_Pause()/Resume() 暂停/恢复
 *   6. Music_Stop()           停止
 * ============================================================ */

/* ---- 音符索引 ----
 * N_xxx 格式: N_音名_八度
 * N_REST = 休止符 (静音)
 * N_END  = 旋律结束标记
 */
typedef enum {
    N_REST = -1,
    N_END = -2,
    N_C3 = 0,  N_CS3, N_D3, N_DS3, N_E3, N_F3, N_FS3, N_G3, N_GS3, N_A3, N_AS3, N_B3,
    N_C4, N_CS4, N_D4, N_DS4, N_E4, N_F4, N_FS4, N_G4, N_GS4, N_A4, N_AS4, N_B4,
    N_C5, N_CS5, N_D5, N_DS5, N_E5, N_F5, N_FS5, N_G5, N_GS5, N_A5, N_AS5, N_B5,
    N_C6,
    N_NOTE_COUNT,
} note_t;

/* ---- 音符结构 ---- */
typedef struct {
    int16_t note;       /* note_t 枚举值, -1=休止, -2=结束 */
    uint8_t beats;      /* 持续节拍数 (1=四分音符, 2=二分, 4=全) */
} note_entry_t;

/* ---- 播放状态 ---- */
typedef enum {
    MUSIC_IDLE,         /* 空闲 */
    MUSIC_PLAYING,      /* 播放中 */
    MUSIC_PAUSED,       /* 暂停 */
    MUSIC_FINISHED,     /* 播放完毕 */
} music_state_t;

/* ---- 曲目信息 ---- */
typedef struct {
    const char *name;                   /* 曲名 */
    const note_entry_t *melody;         /* 旋律数组 */
    int length;                         /* 旋律长度(音符数) */
    uint16_t tempo;                     /* 速度 (每分钟节拍数 BPM) */
} track_t;

/* ---- 初始化 (初始化 DAC) ---- */
void Music_Init(void);

/* ---- 获取曲目数量 ---- */
int Music_GetTrackCount(void);

/* ---- 获取曲目信息 ---- */
const track_t *Music_GetTrack(int idx);

/* ---- 加载曲目 ---- */
void Music_Load(int idx);

/* ---- 播放 ---- */
void Music_Play(void);

/* ---- 暂停 ---- */
void Music_Pause(void);

/* ---- 恢复 ---- */
void Music_Resume(void);

/* ---- 停止 ---- */
void Music_Stop(void);

/* ---- 每帧调用,推进播放 (返回 1=有音符变化, 需更新显示) ---- */
int Music_Update(void);

/* ---- 获取当前状态 ---- */
music_state_t Music_GetState(void);

/* ---- 获取当前播放的音符索引(旋律中位置) ---- */
int Music_GetCurrentNoteIdx(void);

/* ---- 获取当前音符名 ---- */
const char *Music_GetCurrentNoteName(void);

/* ---- 设置音量 (0~100) ---- */
void Music_SetVolume(uint8_t vol);

/* ---- 获取当前曲目索引 ---- */
int Music_GetCurrentTrack(void);

#endif /* MUSIC_H */
