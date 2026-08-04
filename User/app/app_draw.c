#include "app_draw.h"
#include "app_manager.h"
#include "gui.h"
#include "lcd.h"
#include "monitor.h"
#include "settings.h"
#include "encoder.h"
#include "file_sys.h"
#include "usart.h"
#include <stddef.h>
#include <string.h>

/* ============================================================
 * 画图应用
 *
 * 界面布局 (480x320 横屏):
 *   ┌──────────────────────────────────┐
 *   │ DRAW  DN  RED  X:240 Y:180  CLR │  y=0~28 状态栏
 *   ├──────────────────────────────────┤
 *   │                                  │
 *   │            画布区域               │  y=28~320
 *   │          （落笔移动画线）         │
 *   │                                  │
 *   └──────────────────────────────────┘
 *
 * 操作:
 *   方向键    移动画笔（步长从设置读取）
 *   OK        切换落笔/抬笔
 *   编码器旋  切换画笔颜色（7 色循环）
 *   编码器按  清空画布
 *   BACK      返回桌面
 *
 * 说明:
 *   - 落笔时移动会画线（从前一位置到当前位置）
 *   - 抬笔时移动不画线，只改变画笔位置
 *   - 画笔位置在状态栏用坐标显示（画布上不显示光标，避免污染画布）
 *   - 退出时自动保存到 SD 卡,重新进入时从 SD 卡加载并重放
 *   - 重新上电后仍可查看之前保存的绘图
 * ============================================================ */

#define SCR_W       480
#define SCR_H       320
#define STATUS_H    28
#define CANVAS_Y0   STATUS_H          /* 画布起始 Y */

#define PEN_STEP    Settings_CursorSensitivity()

/* ---- 画图数据持久化（SD 卡存储） ---- */
/* 存储布局: Block 17~56 (40 块, 20480 字节)
 *   第一块前 8 字节: magic(4B) + op_count(4B)
 *   后续: draw_op_t 数组 */
#define DRAW_BLOCK_START    17
#define DRAW_BLOCK_COUNT    40
#define DRAW_MAX_OPS        2040    /* (40*512 - 8) / 10 = 2047, 取整 2040 */
#define DRAW_MAGIC          0x57524144  /* "DRAW" */

typedef struct {
    int16_t  x1, y1, x2, y2;
    uint16_t color;
} draw_op_t;  /* 10 字节 */

/* ---- 颜色调色板 ---- */
static const uint16_t palette[] = {
    WHITE, RED, GREEN, BLUE, YELLOW, CYAN, MAGENTA
};
static const char *color_names[] = {
    "WHT", "RED", "GRN", "BLU", "YLW", "CYN", "MGT"
};
#define PALETTE_COUNT   (sizeof(palette) / sizeof(palette[0]))

/* ---- 模块状态 ---- */
static int need_redraw;
static int pen_down;                /* 0=抬笔, 1=落笔 */
static int color_idx;               /* 当前颜色索引 */
static int16_t pen_x, pen_y;        /* 画笔当前位置 */
static int16_t prev_pen_x, prev_pen_y;  /* 上次位置（画线起点） */
static key_state_t prev_key;

/* ---- 画图操作记录（用于持久化） ---- */
static draw_op_t draw_ops[DRAW_MAX_OPS];  /* 操作数组 (约 20KB) */
static int draw_op_count;                  /* 当前操作数 */
static int draw_loaded;                    /* 是否已从 SD 卡加载过 */
static int open_requested;                 /* FILE 应用请求打开绘图 */

/* ---- 子状态 ---- */
typedef enum {
    DRAW_NORMAL,     /* 正常画图 */
    DRAW_CONFIRM,    /* 退出保存确认 */
} draw_substate_t;
static draw_substate_t draw_state;
static int confirm_sel;       /* 确认框选择: 0=NO 1=YES */
static int need_save;         /* 退出时是否需要保存 */

/* ---- 保存画图操作到 SD 卡 ---- */
static void draw_save(void)
{
    uint8_t buf[512];
    uint8_t *ptr = (uint8_t *)draw_ops;
    int total_bytes = draw_op_count * (int)sizeof(draw_op_t);
    int i, offset = 0;

    if (!FileSys_IsReady()) return;

    /* 第一块: magic(4B) + op_count(4B) + 数据(504B) */
    memset(buf, 0, sizeof(buf));
    *(uint32_t *)buf = DRAW_MAGIC;
    *(uint32_t *)(buf + 4) = (uint32_t)draw_op_count;
    {
        int data_in_first = 512 - 8;
        if (data_in_first > total_bytes) data_in_first = total_bytes;
        if (data_in_first > 0) memcpy(buf + 8, ptr, data_in_first);
        FileSys_WriteRawBlock(DRAW_BLOCK_START, buf);
        offset = data_in_first;
    }

    /* 后续块: 每块 512 字节 */
    for (i = 1; i < DRAW_BLOCK_COUNT && offset < total_bytes; i++)
    {
        int chunk = 512;
        if (offset + chunk > total_bytes) chunk = total_bytes - offset;
        memset(buf, 0, sizeof(buf));
        memcpy(buf, ptr + offset, chunk);
        FileSys_WriteRawBlock(DRAW_BLOCK_START + i, buf);
        offset += chunk;
    }

    Log_Printf("[DRAW] saved %d ops to SD\r\n", draw_op_count);

    /* 在文件表中注册绘图文件（供 FILE 应用查看和打开） */
    FileSys_CreateDraw("DRAW.BIN", DRAW_BLOCK_START, (uint32_t)(total_bytes));
}

/* ---- 从 SD 卡加载画图操作 ---- */
static void draw_load(void)
{
    uint8_t buf[512];
    uint8_t *ptr = (uint8_t *)draw_ops;
    int i, offset = 0;
    uint32_t magic, op_count, total_bytes;

    draw_op_count = 0;

    if (!FileSys_IsReady()) return;

    /* 读取第一块 */
    if (FileSys_ReadRawBlock(DRAW_BLOCK_START, buf) != 0) return;

    magic   = *(uint32_t *)buf;
    op_count = *(uint32_t *)(buf + 4);

    if (magic != DRAW_MAGIC || op_count > DRAW_MAX_OPS)
    {
        Log_Printf("[DRAW] no saved data (magic=%08X)\r\n", (unsigned)magic);
        return;
    }

    /* 复制第一块的数据部分 */
    total_bytes = op_count * sizeof(draw_op_t);
    {
        int data_in_first = 512 - 8;
        if (data_in_first > (int)total_bytes) data_in_first = (int)total_bytes;
        if (data_in_first > 0) memcpy(ptr, buf + 8, data_in_first);
        offset = data_in_first;
    }

    /* 读取后续块 */
    for (i = 1; i < DRAW_BLOCK_COUNT && offset < (int)total_bytes; i++)
    {
        int chunk = 512;
        if (FileSys_ReadRawBlock(DRAW_BLOCK_START + i, buf) != 0) break;
        if (offset + chunk > (int)total_bytes) chunk = total_bytes - offset;
        memcpy(ptr + offset, buf, chunk);
        offset += chunk;
    }

    draw_op_count = (int)op_count;
    Log_Printf("[DRAW] loaded %d ops from SD\r\n", draw_op_count);
}

/* ---- 重放所有画图操作（恢复画布内容） ---- */
static void draw_replay(void)
{
    int i;
    for (i = 0; i < draw_op_count; i++)
    {
        g_point_color = draw_ops[i].color;
        LCD_DrawLine(draw_ops[i].x1, draw_ops[i].y1,
                     draw_ops[i].x2, draw_ops[i].y2);
    }
}

/* ---- 画退出确认框 ---- */
static void draw_confirm(void)
{
    /* 半透明遮罩（简化:用深灰填充中央区域） */
    LCD_Fill(80, 110, SCR_W - 80, 220, BLACK);
    GUI_DrawRect(80, 110, SCR_W - 160, 110, WHITE);

    /* 提示文字 */
    GUI_DrawString(140, 125, "Save drawing?", WHITE, BLACK, 2);

    /* NO / YES 按钮 */
    {
        int no_x = 130, yes_x = 260, btn_y = 175, btn_w = 70, btn_h = 30;

        /* NO */
        LCD_Fill(no_x, btn_y, no_x + btn_w, btn_y + btn_h,
                (confirm_sel == 0) ? YELLOW : BLACK);
        GUI_DrawRect(no_x, btn_y, btn_w, btn_h, WHITE);
        GUI_DrawString(no_x + 20, btn_y + 10, "NO",
                       (confirm_sel == 0) ? BLACK : WHITE,
                       (confirm_sel == 0) ? YELLOW : BLACK, 1);

        /* YES */
        LCD_Fill(yes_x, btn_y, yes_x + btn_w, btn_y + btn_h,
                (confirm_sel == 1) ? GREEN : BLACK);
        GUI_DrawRect(yes_x, btn_y, btn_w, btn_h, WHITE);
        GUI_DrawString(yes_x + 14, btn_y + 10, "YES",
                       (confirm_sel == 1) ? BLACK : WHITE,
                       (confirm_sel == 1) ? GREEN : BLACK, 1);
    }
}

/* ---- 画状态栏 ---- */
static void draw_status(void)
{
    LCD_Fill(0, 0, SCR_W - 1, STATUS_H - 1, BLACK);

    /* 标题 */
    GUI_DrawString(5, 8, "DRAW", CYAN, BLACK, 1);

    /* 画笔状态 DN/UP */
    GUI_DrawString(50, 8, pen_down ? "DN" : "UP",
                   pen_down ? GREEN : GRAY, BLACK, 1);

    /* 当前颜色名（用画笔颜色显示） */
    GUI_DrawString(80, 8, color_names[color_idx],
                   palette[color_idx], BLACK, 1);

    /* 画笔坐标 */
    GUI_DrawString(120, 8, "X:", WHITE, BLACK, 1);
    GUI_DrawNum(135, 8, pen_x, WHITE, BLACK, 1);
    GUI_DrawString(175, 8, "Y:", WHITE, BLACK, 1);
    GUI_DrawNum(190, 8, pen_y, WHITE, BLACK, 1);

    /* 清空提示 */
    GUI_DrawString(SCR_W - 55, 8, "EC=CLR", GRAY, BLACK, 1);
}

/* ---- 全屏重绘 ---- */
static void redraw_all(void)
{
    LCD_Clear(BLACK);
    draw_status();
    /* 重放所有操作恢复画布内容 */
    draw_replay();
}

/* ============================================================
 * 应用入口
 * ============================================================ */

void app_draw_create(void)
{
    /* 首次创建：画笔默认在画布中央 */
    pen_x = SCR_W / 2;
    pen_y = (CANVAS_Y0 + SCR_H) / 2;
}

void app_draw_start(void)
{
    /* 从 FILE 应用请求打开：强制重新加载 */
    if (open_requested)
    {
        draw_loaded = 0;
        open_requested = 0;
    }

    /* 首次进入：从 SD 卡加载保存的画图 */
    if (!draw_loaded)
    {
        draw_load();
        draw_loaded = 1;
    }
    draw_state = DRAW_NORMAL;
    need_save  = 0;
    need_redraw = 1;
}

void app_draw_run(key_state_t *key)
{
    uint8_t e_up    = (!prev_key.up)    && key->up;
    uint8_t e_down  = (!prev_key.down)  && key->down;
    uint8_t e_left  = (!prev_key.left)  && key->left;
    uint8_t e_right = (!prev_key.right) && key->right;
    uint8_t e_ok    = (!prev_key.ok)    && key->ok;
    uint8_t e_back  = (!prev_key.back)  && key->back;
    uint8_t e_ecsw  = (!prev_key.ec_sw) && key->ec_sw;
    int16_t enc_delta = Encoder_GetDelta();
    int16_t moved = 0;

    /* ---- 确认状态: 处理保存选择 ---- */
    if (draw_state == DRAW_CONFIRM)
    {
        if (e_left || e_right)
        {
            confirm_sel = !confirm_sel;
            draw_confirm();
            Monitor_IncInputEvent();
        }
        if (e_ok)
        {
            need_save = confirm_sel;
            AppManager_GotoDesktop();
            prev_key = *key;
            return;
        }
        if (e_back)
        {
            /* 取消退出，回到画图 */
            draw_state = DRAW_NORMAL;
            need_redraw = 1;
            prev_key = *key;
            return;
        }
        prev_key = *key;
        return;
    }

    /* ---- 正常画图状态 ---- */

    /* BACK: 弹出保存确认框 */
    if (e_back)
    {
        draw_state = DRAW_CONFIRM;
        confirm_sel = 1;  /* 默认选 YES */
        draw_confirm();
        prev_key = *key;
        return;
    }
    int status_changed = 0;

    /* 输入事件计数 */
    if (e_up || e_down || e_left || e_right || e_ok || e_ecsw)
        Monitor_IncInputEvent();

    /* 首次进入全屏重绘 */
    if (need_redraw)
    {
        redraw_all();
        need_redraw = 0;
        prev_key = *key;
        return;
    }

    /* ---- 画笔移动 ---- */
    prev_pen_x = pen_x;
    prev_pen_y = pen_y;

    if (e_left)  { pen_x -= PEN_STEP; moved = 1; }
    if (e_right) { pen_x += PEN_STEP; moved = 1; }
    if (e_up)    { pen_y -= PEN_STEP; moved = 1; }
    if (e_down)  { pen_y += PEN_STEP; moved = 1; }

    if (moved)
    {
        /* 边界限制：画笔只在画布区域内 */
        if (pen_x < 0) pen_x = 0;
        if (pen_y < CANVAS_Y0) pen_y = CANVAS_Y0;
        if (pen_x >= SCR_W) pen_x = SCR_W - 1;
        if (pen_y >= SCR_H) pen_y = SCR_H - 1;

        /* 落笔时画线（从前一位置到当前位置） */
        if (pen_down)
        {
            g_point_color = palette[color_idx];
            LCD_DrawLine(prev_pen_x, prev_pen_y, pen_x, pen_y);

            /* 记录操作用于持久化 */
            if (draw_op_count < DRAW_MAX_OPS)
            {
                draw_ops[draw_op_count].x1 = prev_pen_x;
                draw_ops[draw_op_count].y1 = prev_pen_y;
                draw_ops[draw_op_count].x2 = pen_x;
                draw_ops[draw_op_count].y2 = pen_y;
                draw_ops[draw_op_count].color = palette[color_idx];
                draw_op_count++;
            }
        }

        status_changed = 1;  /* 坐标变了，更新状态栏 */
    }

    /* ---- OK 键：切换落笔/抬笔 ---- */
    if (e_ok)
    {
        pen_down = !pen_down;
        status_changed = 1;
    }

    /* ---- 编码器旋转：切换颜色 ---- */
    if (enc_delta > 0)
    {
        color_idx = (color_idx + 1) % PALETTE_COUNT;
        status_changed = 1;
    }
    else if (enc_delta < 0)
    {
        color_idx = (color_idx - 1 + PALETTE_COUNT) % PALETTE_COUNT;
        status_changed = 1;
    }

    /* ---- 编码器按压：清空画布 ---- */
    if (e_ecsw)
    {
        LCD_Fill(0, CANVAS_Y0, SCR_W - 1, SCR_H - 1, BLACK);
        draw_op_count = 0;  /* 清空操作记录 */
        Monitor_IncInputEvent();
    }

    /* ---- 状态栏更新 ---- */
    if (status_changed)
        draw_status();

    prev_key = *key;
}

void app_draw_pause(void)
{
    /* 被切换走：根据用户选择决定是否保存 */
    if (need_save)
    {
        draw_save();
    }
    else
    {
        Log_Printf("[DRAW] exit without save\r\n");
    }
    need_save = 0;
    need_redraw = 1;
}

/* ---- FILE 应用请求打开绘图 ---- */
void app_draw_request_open(void)
{
    open_requested = 1;
}

int app_draw_is_open_requested(void)
{
    return open_requested;
}
