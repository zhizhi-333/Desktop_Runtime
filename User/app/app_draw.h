#ifndef APP_DRAW_H
#define APP_DRAW_H

#include "app_manager.h"

/* 画图应用入口（供注册表使用） */
void app_draw_create(void);
void app_draw_start(void);
void app_draw_run(key_state_t *key);
void app_draw_pause(void);

/* 请求从 FILE 应用打开绘图（设置标志，AppManager 检测后自动启动 DRAW） */
void app_draw_request_open(void);

/* 查询是否有待打开的绘图请求（供 AppManager 检测） */
int  app_draw_is_open_requested(void);

#endif /* APP_DRAW_H */
