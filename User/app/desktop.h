#ifndef DESKTOP_H
#define DESKTOP_H

#include "key.h"

/* ---- 桌面接口 ---- */

/* 初始化桌面（首次进入时调用，全屏重绘） */
void Desktop_Init(void);

/* 从应用返回桌面时调用（恢复桌面显示） */
void Desktop_Resume(void);

/* 桌面运行逻辑（每帧调用）
 * 返回值：>=0 表示用户选中了第几个应用，<0 表示无选择 */
int Desktop_Run(key_state_t *key);

#endif /* DESKTOP_H */
