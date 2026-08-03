#ifndef APP_MUSIC_H
#define APP_MUSIC_H

#include "app_manager.h"

/* 音乐播放应用入口（供注册表使用） */
void app_music_create(void);
void app_music_start(void);
void app_music_run(key_state_t *key);
void app_music_pause(void);

#endif /* APP_MUSIC_H */
