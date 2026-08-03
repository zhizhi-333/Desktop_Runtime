#include "music_task.h"
#include "usart.h"
#include <string.h>

/* ============================================================
 * 音乐播放后台任务实现
 *
 * 任务优先级: 2 (与 FileTask 同级,低于 InputTask=3)
 *   - 音乐实时性要求不高,但需要稳定推进
 *   - 输入响应最优先
 *
 * 栈大小: 512 字 (2KB)
 *   - 只调用 Music_Update 和 DAC 接口,栈消耗小
 * ============================================================ */

#define MUSIC_TASK_PRIORITY     2
#define MUSIC_TASK_STACK_SIZE   512
#define MUSIC_CMD_QUEUE_LEN     4
#define MUSIC_TICK_MS           20   /* 每 20ms 推进一次 */

static TaskHandle_t  music_task_handle;
static QueueHandle_t music_cmd_queue;

/* ---- 任务函数 ---- */
static void MusicTask(void *arg)
{
    music_cmd_t cmd;
    TickType_t  last_wake;

    (void)arg;
    Log_Printf("[MUSIC] task entered\r\n");

    last_wake = xTaskGetTickCount();

    for (;;)
    {
        /* 处理所有待处理命令(非阻塞) */
        while (xQueueReceive(music_cmd_queue, &cmd, 0) == pdTRUE)
        {
            switch (cmd.cmd)
            {
                case MUSIC_CMD_LOAD:
                    Music_Load(cmd.param1);
                    Log_Printf("[MUSIC] LOAD track=%d\r\n", cmd.param1);
                    break;
                case MUSIC_CMD_PLAY:
                    Music_Play();
                    Log_Printf("[MUSIC] PLAY\r\n");
                    break;
                case MUSIC_CMD_PAUSE:
                    Music_Pause();
                    Log_Printf("[MUSIC] PAUSE\r\n");
                    break;
                case MUSIC_CMD_RESUME:
                    Music_Resume();
                    Log_Printf("[MUSIC] RESUME\r\n");
                    break;
                case MUSIC_CMD_STOP:
                    Music_Stop();
                    Log_Printf("[MUSIC] STOP\r\n");
                    break;
                case MUSIC_CMD_VOLUME:
                    Music_SetVolume((uint8_t)cmd.param1);
                    break;
                default:
                    break;
            }
        }

        /* 推进播放 */
        Music_Update();

        /* 固定周期 20ms */
        vTaskDelayUntil(&last_wake, pdMS_TO_TICKS(MUSIC_TICK_MS));
    }
}

/* ---- 启动任务 ---- */
void MusicTask_Start(void)
{
    music_cmd_queue = xQueueCreate(MUSIC_CMD_QUEUE_LEN, sizeof(music_cmd_t));

    xTaskCreate(MusicTask, "MusicTask", MUSIC_TASK_STACK_SIZE, NULL,
                MUSIC_TASK_PRIORITY, &music_task_handle);

    Log_Printf("[MUSIC] task started, cmd_queue=%d\r\n", MUSIC_CMD_QUEUE_LEN);
}

/* ---- 发送命令 ---- */
int MusicTask_SendCmd(music_cmd_type_t cmd, int param1, TickType_t timeout)
{
    music_cmd_t c;
    if (music_cmd_queue == NULL) return 0;
    c.cmd    = cmd;
    c.param1 = param1;
    return (xQueueSend(music_cmd_queue, &c, timeout) == pdTRUE) ? 1 : 0;
}

/* ---- 队列水位 ---- */
int MusicTask_GetCmdWatermark(void)
{
    if (music_cmd_queue == NULL) return 0;
    return (int)uxQueueMessagesWaiting(music_cmd_queue);
}

/* ---- 任务栈剩余 ---- */
uint32_t MusicTask_GetStackWatermark(void)
{
    if (music_task_handle == NULL) return 0;
    return (uint32_t)uxTaskGetStackHighWaterMark(music_task_handle);
}
