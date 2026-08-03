#ifndef APP_SETTIME_H
#define APP_SETTIME_H

#include "app_manager.h"

/* 时间设置应用入口（供注册表使用） */
void app_settime_create(void);
void app_settime_start(void);
void app_settime_run(key_state_t *key);
void app_settime_pause(void);

#endif /* APP_SETTIME_H */
