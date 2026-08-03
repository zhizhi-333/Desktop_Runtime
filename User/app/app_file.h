#ifndef APP_FILE_H
#define APP_FILE_H

#include "app_manager.h"

/* 文件管理应用入口（供注册表使用） */
void app_file_create(void);
void app_file_start(void);
void app_file_run(key_state_t *key);
void app_file_pause(void);

#endif /* APP_FILE_H */
