#include "file_task.h"
#include "file_sys.h"
#include "usart.h"
#include "monitor.h"
#include <string.h>

/* ============================================================
 * 文件操作后台任务实现
 *
 * 任务优先级: 2 (低于 InputTask=3,高于 MonitorTask=1)
 *   - 输入响应最优先
 *   - 文件 I/O 次之,但需要及时完成以免应用长时间等待
 *   - 监控最低
 *
 * 栈大小: 1024 字 (4KB)
 *   - FileSys_Read/Write 内部有 512 字节块缓冲,需要足够栈
 * ============================================================ */

#define FILE_TASK_PRIORITY      2
#define FILE_TASK_STACK_SIZE    1024
#define FILE_REQ_QUEUE_LEN      4
#define FILE_RESP_QUEUE_LEN     4

static TaskHandle_t    file_task_handle;
static QueueHandle_t   file_req_queue;
static QueueHandle_t   file_resp_queue;

/* ---- 任务函数 ---- */
static void FileTask(void *arg)
{
    file_req_t  req;
    file_resp_t resp;

    (void)arg;
    Log_Printf("[FILE] task entered\r\n");

    for (;;)
    {
        /* 等待请求(有限超时,确保即使无请求也能定期喂心跳) */
        if (xQueueReceive(file_req_queue, &req, pdMS_TO_TICKS(1000)) != pdTRUE)
        {
            Monitor_Heartbeat(HB_FILE);  /* 空转也喂心跳,防止看门狗误判 */
            continue;
        }

        memset(&resp, 0, sizeof(resp));
        resp.op  = req.op;
        resp.buf = req.buf;

        switch (req.op)
        {
            case FILE_OP_INIT:
                resp.result = FileSys_Init();
                Log_Printf("[FILE] INIT result=%d\r\n", resp.result);
                break;

            case FILE_OP_READ:
                resp.result = FileSys_Read(req.idx, req.buf, req.len);
                resp.len    = resp.result;
                Log_Printf("[FILE] READ idx=%d len=%d\r\n", req.idx, resp.result);
                break;

            case FILE_OP_WRITE:
                resp.result = FileSys_Write(req.idx, req.buf, req.len);
                resp.len    = resp.result;
                Log_Printf("[FILE] WRITE idx=%d len=%d\r\n", req.idx, resp.result);
                break;

            case FILE_OP_CREATE:
                resp.result = FileSys_Create(req.name);
                Log_Printf("[FILE] CREATE name=%s result=%d\r\n", req.name, resp.result);
                break;

            case FILE_OP_DELETE:
                resp.result = FileSys_Delete(req.idx);
                Log_Printf("[FILE] DELETE idx=%d result=%d\r\n", req.idx, resp.result);
                break;

            case FILE_OP_RENAME:
                resp.result = FileSys_Rename(req.idx, req.name);
                Log_Printf("[FILE] RENAME idx=%d name=%s result=%d\r\n",
                           req.idx, req.name, resp.result);
                break;

            default:
                resp.result = -1;
                break;
        }

        /* 心跳上报(发送响应前,避免队列满阻塞时心跳停滞) */
        Monitor_Heartbeat(HB_FILE);

        /* 发送响应(有限超时,防止响应队列满时无限阻塞触发看门狗) */
        if (xQueueSend(file_resp_queue, &resp, pdMS_TO_TICKS(1000)) != pdTRUE)
        {
            Log_Printf("[FILE] resp queue full, dropping response\r\n");
            /* 队列满时丢弃响应, InputTask 的 FS_WAIT 会超时重试 */
        }
    }
}

/* ---- 启动任务 ---- */
void FileTask_Start(void)
{
    file_req_queue  = xQueueCreate(FILE_REQ_QUEUE_LEN, sizeof(file_req_t));
    file_resp_queue = xQueueCreate(FILE_RESP_QUEUE_LEN, sizeof(file_resp_t));

    xTaskCreate(FileTask, "FileTask", FILE_TASK_STACK_SIZE, NULL,
                FILE_TASK_PRIORITY, &file_task_handle);

    Log_Printf("[FILE] task started, req_queue=%d resp_queue=%d\r\n",
               FILE_REQ_QUEUE_LEN, FILE_RESP_QUEUE_LEN);
}

/* ---- 投递请求 ---- */
int FileTask_Request(const file_req_t *req, TickType_t timeout)
{
    if (file_req_queue == NULL) return 0;
    return (xQueueSend(file_req_queue, req, timeout) == pdTRUE) ? 1 : 0;
}

/* ---- 获取响应 ---- */
int FileTask_GetResponse(file_resp_t *resp, TickType_t timeout)
{
    if (file_resp_queue == NULL) return 0;
    return (xQueueReceive(file_resp_queue, resp, timeout) == pdTRUE) ? 1 : 0;
}

/* ---- 队列水位 ---- */
int FileTask_GetReqWatermark(void)
{
    if (file_req_queue == NULL) return 0;
    return (int)uxQueueMessagesWaiting(file_req_queue);
}

int FileTask_GetRespWatermark(void)
{
    if (file_resp_queue == NULL) return 0;
    return (int)uxQueueMessagesWaiting(file_resp_queue);
}

/* ---- 任务栈剩余 ---- */
uint32_t FileTask_GetStackWatermark(void)
{
    if (file_task_handle == NULL) return 0;
    return (uint32_t)uxTaskGetStackHighWaterMark(file_task_handle);
}

/* ---- 任务运行状态 ---- */
eTaskState FileTask_GetState(void)
{
    if (file_task_handle == NULL) return eDeleted;
    return eTaskGetState(file_task_handle);
}
