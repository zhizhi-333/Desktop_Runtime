#include "app_music.h"
#include "app_manager.h"
#include "gui.h"
#include "lcd.h"
#include "monitor.h"
#include "settings.h"
#include "music.h"
#include "music_task.h"
#include "dac.h"
#include "encoder.h"
#include <stddef.h>
#include <stdio.h>
#include <string.h>

/* ============================================================
 * 音乐播放应用
 *
 * 界面布局 (480x320):
 *   ┌──────────────────────────────────┐
 *   │ MUSIC PLAYER                     │  标题
 *   ├──────────────────────────────────┤
 *   │ > 1. Two Tigers                  │  曲目列表
 *   │   2. Little Star                 │  (↑↓ 选择)
 *   │   3. Happy B-Day                 │
 *   │                                  │
 *   ├──────────────────────────────────┤
 *   │ Now: Two Tigers                  │  当前曲目
 *   │ Note: C4   [####------] 4/34     │  播放进度
 *   │ State: PLAYING                   │  状态
 *   ├──────────────────────────────────┤
 *   │ OK:Play/Pause  EC:Vol+  EC_SW:Stop│  操作提示
 *   │ BACK:Exit                        │
 *   └──────────────────────────────────┘
 *
 * 操作:
 *   ↑↓      选择曲目
 *   OK      播放/暂停
 *   编码器旋 调节音量
 *   编码器按 停止播放
 *   BACK    退出
 *
 * 退出处理:
 *   - on_pause 时停止播放,避免后台继续发声
 *   - 音量关联系统设置 (Settings_Volume)
 * ============================================================ */

#define SCR_W       480
#define SCR_H       320
#define STATUS_H    28
#define LIST_Y0     35
#define LIST_ROW_H  22
#define INFO_Y      160
#define FOOTER_Y    260

/* ---- 颜色 ---- */
#define CLR_BG          BLACK
#define CLR_TITLE       CYAN
#define CLR_SEL         YELLOW
#define CLR_TEXT        WHITE
#define CLR_HINT        GRAY
#define CLR_PLAY        GREEN
#define CLR_PAUSE       YELLOW
#define CLR_STOP        GRAY
#define CLR_NOTE        MAGENTA

/* ---- 模块状态 ---- */
static int need_redraw;
static int sel_track;           /* 列表选中曲目 */
static int music_initialized;
static key_state_t prev_key;
static int prev_enc_delta;
static int last_note_idx;       /* 上次显示的音符索引(检测变化) */
static music_state_t last_state;/* 上次显示的状态 */

/* ---- 增量更新跟踪变量 ---- */
static int prev_filled;              /* 上次进度条填充宽度 */
static char prev_note_name[8];       /* 上次音符名 */
static uint8_t prev_vol;             /* 上次音量 */
static uint32_t prev_isr_count;      /* 上次ISR计数(诊断) */

/* ---- 画曲目列表 ---- */
static void draw_track_list(void)
{
    int i, count;

    count = Music_GetTrackCount();
    for (i = 0; i < count; i++)
    {
        const track_t *t = Music_GetTrack(i);
        char line[32];
        int y = LIST_Y0 + i * LIST_ROW_H;

        snprintf(line, sizeof(line), "%s%d. %s",
                 (i == sel_track) ? "> " : "  ",
                 i + 1, t->name);

        GUI_DrawString(10, y, line,
                       (i == sel_track) ? CLR_SEL : CLR_TEXT,
                       CLR_BG, 1);
    }
}

/* ---- 进度条几何参数 ---- */
#define BAR_X       130
#define BAR_Y       (INFO_Y + 20)
#define BAR_W       200
#define BAR_H       8

/* 前向声明 */
static void draw_info_dynamic(void);

/* ---- 画播放信息区(全量重绘, 仅首次/重入时调用) ---- */
static void draw_info_full(void)
{
    const track_t *t = Music_GetTrack(Music_GetCurrentTrack());
    char buf[40];

    /* 清除信息区 */
    LCD_Fill(0, INFO_Y, SCR_W - 1, FOOTER_Y - 1, CLR_BG);

    /* 分隔线 */
    GUI_DrawRect(0, INFO_Y - 2, SCR_W, 2, CLR_HINT);

    /* 当前曲目名(静态, 播放期间不变) */
    if (t)
    {
        snprintf(buf, sizeof(buf), "Now: %s", t->name);
        GUI_DrawString(10, INFO_Y, buf, CLR_TEXT, CLR_BG, 1);
    }

    /* 进度条背景(静态框架) */
    LCD_Fill(BAR_X, BAR_Y, BAR_X + BAR_W, BAR_Y + BAR_H, CLR_HINT);

    /* 固定标签 */
    GUI_DrawString(10, INFO_Y + 40, "State:", CLR_HINT, CLR_BG, 1);
    GUI_DrawString(250, INFO_Y + 40, "Vol:", CLR_HINT, CLR_BG, 1);
    GUI_DrawString(360, INFO_Y + 40, "ISR:", CLR_HINT, CLR_BG, 1);

    /* 重置增量跟踪 */
    prev_filled = -1;
    prev_note_name[0] = '\0';
    prev_vol = 0xFF;
    prev_isr_count = 0xFFFFFFFF;
    last_state = (music_state_t)-1;

    /* 首次绘制动态内容 */
    draw_info_dynamic();
}

/* ---- 增量更新播放信息(每次循环调用, 只重绘变化部分) ---- */
static void draw_info_dynamic(void)
{
    const track_t *t = Music_GetTrack(Music_GetCurrentTrack());
    music_state_t st = Music_GetState();
    int note_idx = Music_GetCurrentNoteIdx();
    const char *note_name = Music_GetCurrentNoteName();
    uint8_t vol = DAC_GetVolume();
    uint32_t isr = DAC_GetIsrCount();
    int track_len = t ? t->length : 1;
    int filled = BAR_W * note_idx / track_len;
    char buf[40];

    /* ---- 音符名: 仅变化时重绘 ---- */
    if (strcmp(prev_note_name, note_name) != 0)
    {
        LCD_Fill(10, BAR_Y, BAR_X - 1, BAR_Y + 16, CLR_BG);
        snprintf(buf, sizeof(buf), "Note: %s", note_name);
        GUI_DrawString(10, BAR_Y, buf, CLR_NOTE, CLR_BG, 1);
        strncpy(prev_note_name, note_name, sizeof(prev_note_name) - 1);
        prev_note_name[sizeof(prev_note_name) - 1] = '\0';
    }

    /* ---- 进度条: 仅变化部分增量更新 ---- */
    if (filled != prev_filled)
    {
        if (filled > prev_filled)
        {
            /* 扩展: 只填充新增部分 */
            if (filled > 0)
                LCD_Fill(BAR_X + (prev_filled > 0 ? prev_filled : 0),
                         BAR_Y, BAR_X + filled, BAR_Y + BAR_H, CLR_PLAY);
        }
        else
        {
            /* 收缩: 只清除减少部分 */
            LCD_Fill(BAR_X + filled, BAR_Y,
                     BAR_X + (prev_filled > 0 ? prev_filled : 0),
                     BAR_Y + BAR_H, CLR_HINT);
        }
        prev_filled = filled;

        /* 更新计数文本 */
        snprintf(buf, sizeof(buf), "%d/%d", note_idx, track_len);
        LCD_Fill(BAR_X + BAR_W + 10, BAR_Y - 2,
                 BAR_X + BAR_W + 60, BAR_Y + 14, CLR_BG);
        GUI_DrawString(BAR_X + BAR_W + 10, BAR_Y - 2, buf, CLR_HINT, CLR_BG, 1);
    }

    /* ---- 状态: 仅变化时重绘 ---- */
    if (st != last_state)
    {
        const char *state_str;
        uint16_t state_color;
        switch (st)
        {
            case MUSIC_IDLE:     state_str = "IDLE";    state_color = CLR_STOP;   break;
            case MUSIC_PLAYING:  state_str = "PLAYING"; state_color = CLR_PLAY;   break;
            case MUSIC_PAUSED:   state_str = "PAUSED";  state_color = CLR_PAUSE;  break;
            case MUSIC_FINISHED: state_str = "DONE";    state_color = CLR_STOP;   break;
            default:             state_str = "????";    state_color = CLR_HINT;   break;
        }
        snprintf(buf, sizeof(buf), "%s", state_str);
        LCD_Fill(55, INFO_Y + 40, 120, INFO_Y + 56, CLR_BG);
        GUI_DrawString(55, INFO_Y + 40, buf, state_color, CLR_BG, 1);
        last_state = st;
    }

    /* ---- 音量: 仅变化时重绘 ---- */
    if (vol != prev_vol)
    {
        snprintf(buf, sizeof(buf), "%d%%", vol);
        LCD_Fill(285, INFO_Y + 40, 340, INFO_Y + 56, CLR_BG);
        GUI_DrawString(285, INFO_Y + 40, buf, CLR_TEXT, CLR_BG, 1);
        prev_vol = vol;
    }

    /* ---- ISR 计数: 诊断用, 播放时应快速递增 ---- */
    if (isr != prev_isr_count)
    {
        snprintf(buf, sizeof(buf), "%u", isr);
        LCD_Fill(395, INFO_Y + 40, 460, INFO_Y + 56, CLR_BG);
        GUI_DrawString(395, INFO_Y + 40, buf, CLR_HINT, CLR_BG, 1);
        prev_isr_count = isr;
    }
}

/* ---- 全屏重绘 ---- */
static void redraw_all(void)
{
    LCD_Clear(CLR_BG);

    /* 标题 */
    GUI_DrawString(5, 8, "MUSIC PLAYER", CLR_TITLE, CLR_BG, 1);

    /* 曲目列表标题 */
    GUI_DrawString(10, LIST_Y0 - 18, "Tracks:", CLR_HINT, CLR_BG, 1);

    /* 曲目列表 */
    draw_track_list();

    /* 播放信息 */
    draw_info_full();

    /* 底部提示 */
    LCD_Fill(0, FOOTER_Y, SCR_W - 1, SCR_H - 1, CLR_BG);
    GUI_DrawString(5, FOOTER_Y,      "OK:Play/Pause  EC:Vol  EC_SW:Stop", CLR_HINT, CLR_BG, 1);
    GUI_DrawString(5, FOOTER_Y + 14, "UP/DN:Select  BACK:Exit", CLR_HINT, CLR_BG, 1);
}

/* ============================================================
 * 应用入口
 * ============================================================ */

void app_music_create(void)
{
    if (!music_initialized)
    {
        Music_Init();
        /* 关联系统音量设置 */
        Music_SetVolume(Settings_Volume());
        music_initialized = 1;
    }
}

void app_music_start(void)
{
    need_redraw = 1;
}

void app_music_run(key_state_t *key)
{
    uint8_t e_up    = (!prev_key.up)    && key->up;
    uint8_t e_down  = (!prev_key.down)  && key->down;
    uint8_t e_ok    = (!prev_key.ok)    && key->ok;
    uint8_t e_back  = (!prev_key.back)  && key->back;
    uint8_t e_ecsw  = (!prev_key.ec_sw) && key->ec_sw;
    int16_t enc_delta = Encoder_GetDelta();
    int need_info_update = 0;

    /* BACK: 停止播放并返回桌面 */
    if (e_back)
    {
        MusicTask_SendCmd(MUSIC_CMD_STOP, 0, 0);
        AppManager_GotoDesktop();
        prev_key = *key;
        return;
    }

    if (e_up || e_down || e_ok || e_ecsw)
        Monitor_IncInputEvent();

    /* 首次进入全屏重绘 */
    if (need_redraw)
    {
        redraw_all();
        need_redraw = 0;
        last_note_idx = -1;
        last_state = (music_state_t)-1;
        prev_key = *key;
        return;
    }

    /* ---- 曲目选择 ---- */
    if (e_up && sel_track > 0)
    {
        sel_track--;
        draw_track_list();
    }
    if (e_down && sel_track < Music_GetTrackCount() - 1)
    {
        sel_track++;
        draw_track_list();
    }

    /* ---- OK: 播放/暂停 (通过队列发命令给 MusicTask) ---- */
    if (e_ok)
    {
        music_state_t st = Music_GetState();
        if (st == MUSIC_PLAYING)
        {
            MusicTask_SendCmd(MUSIC_CMD_PAUSE, 0, 0);
        }
        else if (st == MUSIC_PAUSED)
        {
            MusicTask_SendCmd(MUSIC_CMD_RESUME, 0, 0);
        }
        else  /* IDLE 或 FINISHED */
        {
            MusicTask_SendCmd(MUSIC_CMD_LOAD, sel_track, 0);
            MusicTask_SendCmd(MUSIC_CMD_PLAY, 0, 0);
        }
        need_info_update = 1;
    }

    /* ---- 编码器按压: 停止 ---- */
    if (e_ecsw)
    {
        MusicTask_SendCmd(MUSIC_CMD_STOP, 0, 0);
        need_info_update = 1;
    }

    /* ---- 编码器旋转: 音量 ---- */
    if (enc_delta > 0)
    {
        uint8_t vol = DAC_GetVolume();
        if (vol < 100) { vol++; MusicTask_SendCmd(MUSIC_CMD_VOLUME, vol, 0); Settings_SetVolume(vol); }
        need_info_update = 1;
    }
    else if (enc_delta < 0)
    {
        uint8_t vol = DAC_GetVolume();
        if (vol > 0) { vol--; MusicTask_SendCmd(MUSIC_CMD_VOLUME, vol, 0); Settings_SetVolume(vol); }
        need_info_update = 1;
    }

    /* ---- 播放推进由 MusicTask 后台执行,这里只检测状态变化 ---- */
    {
        music_state_t cur_state = Music_GetState();
        int cur_note = Music_GetCurrentNoteIdx();

        if (cur_state != last_state || cur_note != last_note_idx)
        {
            need_info_update = 1;
            last_state = cur_state;
            last_note_idx = cur_note;
        }
    }

    /* ---- 增量刷新信息区(只重绘变化部分, 避免闪烁) ---- */
    draw_info_dynamic();

    prev_key = *key;
}

void app_music_pause(void)
{
    /* 被切换走:停止播放,避免后台发声 */
    MusicTask_SendCmd(MUSIC_CMD_STOP, 0, 0);
    need_redraw = 1;
}
