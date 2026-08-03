#ifndef APP_SETTINGS_H
#define APP_SETTINGS_H

#include "app_manager.h"

/* 系统设置应用入口（供注册表使用） */
void app_setting_create(void);
void app_setting_start(void);
void app_setting_run(key_state_t *key);
void app_setting_pause(void);

#endif /* APP_SETTINGS_H */
