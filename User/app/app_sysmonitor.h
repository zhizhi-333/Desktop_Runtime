#ifndef APP_SYSMONITOR_H
#define APP_SYSMONITOR_H

#include "app_manager.h"

/* 系统监控应用入口（供注册表使用） */
void app_sysmonitor_create(void);
void app_sysmonitor_start(void);
void app_sysmonitor_run(key_state_t *key);
void app_sysmonitor_pause(void);

#endif /* APP_SYSMONITOR_H */
