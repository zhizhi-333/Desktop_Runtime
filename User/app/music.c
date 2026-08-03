#include "music.h"
#include "dac.h"
#include <stddef.h>

/* ============================================================
 * 音乐播放引擎实现
 *
 * 音符频率表 (12 平均律, A4=440Hz):
 *   每升高一个半音, 频率 *= 2^(1/12)
 *
 * 播放逻辑:
 *   - Music_Update() 每 20ms 调用一次 (由 app_music 驱动)
 *   - 每个音符持续 beats * (60000 / tempo / 20) 帧
 *   - 播完一个音符切到下一个, 遇 N_END 结束
 * ============================================================ */

/* ---- 音符频率表 (C3~C6, 共 37 个) ---- */
static const uint16_t note_freq[N_NOTE_COUNT] = {
    /* C3~B3 (131~247 Hz) */
    131, 139, 147, 156, 165, 175, 185, 196, 208, 220, 233, 247,
    /* C4~B4 (262~494 Hz) */
    262, 277, 294, 311, 330, 349, 370, 392, 415, 440, 466, 494,
    /* C5~B5 (523~988 Hz) */
    523, 554, 587, 622, 659, 698, 740, 784, 831, 880, 932, 988,
    /* C6 (1047 Hz) */
    1047,
};

/* ---- 音符名表 ---- */
static const char *note_names[N_NOTE_COUNT] = {
    "C3 ","C#3","D3 ","D#3","E3 ","F3 ","F#3","G3 ","G#3","A3 ","A#3","B3 ",
    "C4 ","C#4","D4 ","D#4","E4 ","F4 ","F#4","G4 ","G#4","A4 ","A#4","B4 ",
    "C5 ","C#5","D5 ","D#5","E5 ","F5 ","F#5","G5 ","G#5","A5 ","A#5","B5 ",
    "C6 ",
};

/* ============================================================
 * 内置曲目
 * ============================================================ */

/* ---- 1. 两只老虎 (C4 调) ---- */
static const note_entry_t melody_twinkle[] = {
    { N_C4, 1 }, { N_D4, 1 }, { N_E4, 1 }, { N_C4, 1 },
    { N_C4, 1 }, { N_D4, 1 }, { N_E4, 1 }, { N_C4, 1 },
    { N_E4, 1 }, { N_F4, 1 }, { N_G4, 2 },
    { N_E4, 1 }, { N_F4, 1 }, { N_G4, 2 },
    { N_G4, 1 }, { N_A4, 1 }, { N_G4, 1 }, { N_A4, 1 },
    { N_G4, 1 }, { N_F4, 1 }, { N_E4, 1 }, { N_C4, 1 },
    { N_G4, 1 }, { N_A4, 1 }, { N_G4, 1 }, { N_A4, 1 },
    { N_G4, 1 }, { N_F4, 1 }, { N_E4, 1 }, { N_C4, 1 },
    { N_C4, 1 }, { N_G3, 1 }, { N_C4, 2 },
    { N_C4, 1 }, { N_G3, 1 }, { N_C4, 2 },
    { N_END, 0 },
};

/* ---- 2. 小星星 ---- */
static const note_entry_t melody_star[] = {
    { N_C4, 1 }, { N_C4, 1 }, { N_G4, 1 }, { N_G4, 1 },
    { N_A4, 1 }, { N_A4, 1 }, { N_G4, 2 },
    { N_F4, 1 }, { N_F4, 1 }, { N_E4, 1 }, { N_E4, 1 },
    { N_D4, 1 }, { N_D4, 1 }, { N_C4, 2 },
    { N_G4, 1 }, { N_G4, 1 }, { N_F4, 1 }, { N_F4, 1 },
    { N_E4, 1 }, { N_E4, 1 }, { N_D4, 2 },
    { N_G4, 1 }, { N_G4, 1 }, { N_F4, 1 }, { N_F4, 1 },
    { N_E4, 1 }, { N_E4, 1 }, { N_D4, 2 },
    { N_C4, 1 }, { N_C4, 1 }, { N_G4, 1 }, { N_G4, 1 },
    { N_A4, 1 }, { N_A4, 1 }, { N_G4, 2 },
    { N_F4, 1 }, { N_F4, 1 }, { N_E4, 1 }, { N_E4, 1 },
    { N_D4, 1 }, { N_D4, 1 }, { N_C4, 2 },
    { N_END, 0 },
};

/* ---- 3. 生日快乐 (片段) ---- */
static const note_entry_t melody_birthday[] = {
    { N_C4, 1 }, { N_C4, 1 }, { N_D4, 1 }, { N_C4, 1 },
    { N_F4, 1 }, { N_E4, 2 },
    { N_C4, 1 }, { N_C4, 1 }, { N_D4, 1 }, { N_C4, 1 },
    { N_G4, 1 }, { N_F4, 2 },
    { N_C4, 1 }, { N_C4, 1 }, { N_C5, 1 }, { N_A4, 1 },
    { N_F4, 1 }, { N_E4, 1 }, { N_D4, 2 },
    { N_AS4, 1 }, { N_AS4, 1 }, { N_A4, 1 }, { N_F4, 1 },
    { N_G4, 1 }, { N_F4, 2 },
    { N_END, 0 },
};

/* ---- 曲目表 ---- */
static const track_t tracks[] = {
    { "Two Tigers",  melody_twinkle,  sizeof(melody_twinkle)/sizeof(note_entry_t),  120 },
    { "Little Star", melody_star,     sizeof(melody_star)/sizeof(note_entry_t),      100 },
    { "Happy B-Day", melody_birthday, sizeof(melody_birthday)/sizeof(note_entry_t), 120 },
};
#define TRACK_COUNT (sizeof(tracks) / sizeof(tracks[0]))

/* ---- 模块状态 ---- */
static music_state_t state = MUSIC_IDLE;
static int current_track;
static int current_note;        /* 旋律中当前音符索引 */
static int frames_remaining;    /* 当前音符剩余帧数 */
static int frames_per_beat;     /* 一拍对应的帧数 */

/* ============================================================
 * 公共接口实现
 * ============================================================ */

void Music_Init(void)
{
    DAC_Init();
    state = MUSIC_IDLE;
    current_track = 0;
    current_note = 0;
}

int Music_GetTrackCount(void)
{
    return (int)TRACK_COUNT;
}

const track_t *Music_GetTrack(int idx)
{
    if (idx < 0 || idx >= (int)TRACK_COUNT) return NULL;
    return &tracks[idx];
}

void Music_Load(int idx)
{
    if (idx < 0 || idx >= (int)TRACK_COUNT) return;
    Music_Stop();
    current_track = idx;
    state = MUSIC_IDLE;
}

void Music_Play(void)
{
    const track_t *t = &tracks[current_track];
    if (state == MUSIC_PAUSED)
    {
        state = MUSIC_PLAYING;
        DAC_Start();
        return;
    }

    current_note = 0;
    frames_per_beat = 60000 / (t->tempo * 20);  /* 20ms 一帧 */
    if (frames_per_beat < 1) frames_per_beat = 1;

    /* 播放第一个音符 */
    {
        const note_entry_t *e = &t->melody[current_note];
        if (e->note == N_REST)
        {
            DAC_Stop();
        }
        else if (e->note == N_END)
        {
            state = MUSIC_FINISHED;
            return;
        }
        else
        {
            DAC_SetFreq(note_freq[e->note]);
            DAC_Start();
        }
        frames_remaining = e->beats * frames_per_beat;
    }
    state = MUSIC_PLAYING;
}

void Music_Pause(void)
{
    if (state == MUSIC_PLAYING)
    {
        state = MUSIC_PAUSED;
        DAC_Stop();
    }
}

void Music_Resume(void)
{
    if (state == MUSIC_PAUSED)
    {
        state = MUSIC_PLAYING;
        DAC_Start();
    }
}

void Music_Stop(void)
{
    DAC_Stop();
    state = MUSIC_IDLE;
    current_note = 0;
}

int Music_Update(void)
{
    const track_t *t;
    const note_entry_t *e;

    if (state != MUSIC_PLAYING) return 0;

    frames_remaining--;
    if (frames_remaining > 0) return 0;

    /* 切到下一个音符 */
    current_note++;
    t = &tracks[current_track];
    e = &t->melody[current_note];

    if (e->note == N_END)
    {
        DAC_Stop();
        state = MUSIC_FINISHED;
        return 1;
    }

    if (e->note == N_REST)
    {
        DAC_Stop();
    }
    else
    {
        DAC_SetFreq(note_freq[e->note]);
        DAC_Start();
    }
    frames_remaining = e->beats * frames_per_beat;
    return 1;  /* 音符变化 */
}

music_state_t Music_GetState(void)
{
    return state;
}

int Music_GetCurrentNoteIdx(void)
{
    return current_note;
}

const char *Music_GetCurrentNoteName(void)
{
    const track_t *t;
    const note_entry_t *e;
    if (state == MUSIC_IDLE || state == MUSIC_FINISHED) return "----";
    t = &tracks[current_track];
    e = &t->melody[current_note];
    if (e->note == N_REST) return "REST";
    if (e->note == N_END)  return "END ";
    if (e->note < 0 || e->note >= N_NOTE_COUNT) return "????";
    return note_names[e->note];
}

void Music_SetVolume(uint8_t vol)
{
    DAC_SetVolume(vol);
}

int Music_GetCurrentTrack(void)
{
    return current_track;
}
