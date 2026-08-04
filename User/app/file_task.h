#ifndef FILE_TASK_H
#define FILE_TASK_H

#include <stdint.h>
#include "FreeRTOS.h"
#include "task.h"
#include "queue.h"

/* ============================================================
 * 文件操作后台任务
 *
 * 目的:
 *   将 SD 卡读写从 InputTask 中剥离,避免块 I/O 阻塞输入响应
 *
 * 工作模式:
 *   - InputTask (app_file) 通过 FileTask_Request() 投递请求
 *   - FileTask 在独立任务中阻塞等待请求队列
 *   - 完成后通过响应队列回传结果
 *   - app_file 每帧非阻塞检查响应,收到才推进状态机
 *
 * 队列水位供 SYSMONITOR 显示
 * ============================================================ */

/* ---- 文件操作类型 ---- */
typedef enum {
    FILE_OP_NONE = 0,
    FILE_OP_INIT,       /* 初始化 SD 卡 + 文件系统 */
    FILE_OP_READ,       /* 读文件内容 */
    FILE_OP_WRITE,      /* 写文件内容 */
    FILE_OP_CREATE,     /* 新建文件 */
    FILE_OP_DELETE,     /* 删除文件 */
    FILE_OP_RENAME,     /* 重命名文件 */
} file_op_type_t;

/* ---- 请求结构体 ---- */
typedef struct {
    file_op_type_t op;
    int            idx;          /* 文件索引 (read/write/delete/rename) */
    uint8_t       *buf;          /* 读写缓冲区指针 (read 时由调用方提供, 任务写入) */
    int            len;          /* write 时为数据长度; read 时为缓冲区最大容量 */
    char           name[13];     /* create/rename 时为文件名 */
} file_req_t;

/* ---- 响应结构体 ---- */
typedef struct {
    file_op_type_t op;
    int            result;       /* >=0 成功(具体语义见下), <0 失败 */
    int            len;          /* read 时为实际读到的字节数 */
    uint8_t       *buf;          /* 回传 buf 指针,便于调用方识别 */
} file_resp_t;

/* ---- 结果语义 ---- */
/* FILE_OP_INIT:   result=0 成功, -1 失败 */
/* FILE_OP_READ:   result=实际字节数(>=0) 成功, -1 失败 */
/* FILE_OP_WRITE:  result=写入字节数(>=0) 成功, -1 失败 */
/* FILE_OP_CREATE: result=文件索引(>=0) 成功, -2 重名, -1 失败 */
/* FILE_OP_DELETE: result=0 成功, -1 失败 */
/* FILE_OP_RENAME: result=0 成功, -2 重名, -1 失败 */

/* ---- 公共接口 ---- */

/* 创建队列 + 任务(在 Monitor_Start 中调用) */
void FileTask_Start(void);

/* 投递请求(发送到请求队列),timeout=0 为非阻塞
 * 返回: 1 成功入队, 0 队列满/超时 */
int FileTask_Request(const file_req_t *req, TickType_t timeout);

/* 非阻塞获取响应(从响应队列)
 * 返回: 1 收到响应, 0 队列空 */
int FileTask_GetResponse(file_resp_t *resp, TickType_t timeout);

/* 查询队列水位(供 SYSMONITOR 显示) */
int FileTask_GetReqWatermark(void);    /* 请求队列当前等待数 */
int FileTask_GetRespWatermark(void);   /* 响应队列当前等待数 */

/* 查询任务栈剩余(字,供 SYSMONITOR 显示) */
uint32_t FileTask_GetStackWatermark(void);

/* 查询任务运行状态(供 SYSMONITOR 显示)
 * 返回 eTaskState: eRunning/eReady/eBlocked/eSuspended/eDeleted */
eTaskState FileTask_GetState(void);

#endif /* FILE_TASK_H */
