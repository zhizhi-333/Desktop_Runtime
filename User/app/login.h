#ifndef LOGIN_H
#define LOGIN_H

#include "key.h"

/* 登录界面状态 */
typedef enum {
    LOGIN_STATE_INPUT,      /* 正在输入密码 */
    LOGIN_STATE_OK,         /* 密码正确，即将进入桌面 */
    LOGIN_STATE_ERR,        /* 密码错误，短暂提示后重试 */
} login_state_t;

/* 初始化登录界面（首次进入时调用，会全屏重绘） */
void Login_Init(void);

/* 锁屏初始化（熄屏唤醒后调用，跳过选择界面直接进入密码输入）
 * 标题显示 LOCKED，密码正确后返回1 */
void Login_LockInit(void);

/* 登录界面运行逻辑（每次 InputTask 循环调用）
 * 返回值：1 = 登录成功，调用方应切换到桌面；0 = 继续在登录界面 */
int Login_Run(key_state_t *key);

/* 查询是否已通过登录（供状态机使用） */
int Login_IsPassed(void);

#endif /* LOGIN_H */
