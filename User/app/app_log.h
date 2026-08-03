#ifndef APP_LOG_H
#define APP_LOG_H

#include "app_manager.h"

/* 日志查看应用入口（供注册表使用） */
void app_log_create(void);
void app_log_start(void);
void app_log_run(key_state_t *key);
void app_log_pause(void);

#endif /* APP_LOG_H */
