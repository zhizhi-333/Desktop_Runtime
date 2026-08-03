#ifndef APP_REGISTRY_H
#define APP_REGISTRY_H

#include "app_manager.h"

/* 获取应用总数 */
int AppRegistry_GetCount(void);

/* 获取应用入口 */
const app_entry_t *AppRegistry_GetApp(int idx);

/* 获取应用状态数组指针（供 AppManager 修改） */
app_state_t *AppRegistry_GetStates(void);

#endif /* APP_REGISTRY_H */
