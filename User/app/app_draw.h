#ifndef APP_DRAW_H
#define APP_DRAW_H

#include "app_manager.h"

/* 画图应用入口（供注册表使用） */
void app_draw_create(void);
void app_draw_start(void);
void app_draw_run(key_state_t *key);
void app_draw_pause(void);

#endif /* APP_DRAW_H */
