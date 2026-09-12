#include "app_file.h"
#include "app_manager.h"
#include "gui.h"
#include "lcd.h"
#include "monitor.h"
#include "settings.h"
#include "file_sys.h"
#include "file_task.h"
#include "app_draw.h"
#include "log_store.h"
#include "FreeRTOS.h"
#include "task.h"
#include <string.h>
#include <stdio.h>

/* ============================================================
 * 文件管理应用
 *
 * 5 个子状态:
 *   FS_LIST   - 文件列表（选文件/新建/删除）
 *   FS_VIEW   - 查看文件内容
 *   FS_EDIT   - 编辑文件内容（字符网格输入）
 *   FS_RENAME - 重命名文件（字符网格输入）
 *   FS_DELETE - 删除确认
 *
 * 操作:
 *   LIST:   ↑↓选择 OK打开 ←新建 →删除 BACK退出
 *   VIEW:   ↑↓滚动 OK编辑 BACK返回列表
 *   EDIT:   ↑↓←→移动字符网格 OK选择 BACK取消
 *   RENAME: 同 EDIT
 *   DELETE: OK确认 BACK取消
 *
 * 性能优化:
 *   - 网格移动/输入用局部刷新（只重绘变化格子 + 内容预览行）
 *   - 全屏重绘只在首次进入或状态切换时发生
 * ============================================================ */

#define SCR_W       480
#define SCR_H       320
#define STATUS_H    28

/* ---- 子状态 ---- */
typedef enum {
    FS_LIST,
    FS_VIEW,
    FS_EDIT,
    FS_RENAME,
    FS_DELETE,
    FS_WAIT,       /* 等待后台文件操作完成 */
} file_substate_t;

/* ---- 字符网格 ---- */
#define GRID_COLS   10
#define GRID_ROWS   4
#define GRID_CELL_W 44
#define GRID_CELL_H 30
#define GRID_X0     20
#define GRID_Y0     90

/* 网格字符表（36 字母数字 + 4 功能键） */
/* index 36=' '(空格) 37='<'(退格) 38='>'(保存) 39='.'(点) */
static const char grid_chars[GRID_COLS * GRID_ROWS] = {
    'A','B','C','D','E','F','G','H','I','J',
    'K','L','M','N','O','P','Q','R','S','T',
    'U','V','W','X','Y','Z','0','1','2','3',
    '4','5','6','7','8','9',' ','<','>','.',
};
#define GRID_IDX_SPACE     36
#define GRID_IDX_BACKSPACE 37
#define GRID_IDX_SAVE      38
#define GRID_IDX_DOT       39

/* ---- 颜色 ---- */
#define CLR_BG          BLACK
#define CLR_TITLE       CYAN
#define CLR_SEL         YELLOW
#define CLR_TEXT        WHITE
#define CLR_HINT        GRAY
#define CLR_BTN         GREEN
#define CLR_WARN        RED

/* ---- 列表区域 ---- */
#define LIST_Y0         40
#define LIST_ROW_H      22
#define LIST_ROWS       8       /* 可见行数 */
#define LIST_FOOTER_Y   240

/* ---- 模块状态 ---- */
static file_substate_t fs_state;
static int need_redraw;             /* 全屏重绘标志 */
static int need_grid_partial;       /* 网格局部刷新标志 */
static int sel_idx;                 /* 列表选中索引 */
static int list_scroll;             /* 列表滚动偏移 */
static int fs_initialized;          /* FileSys 是否已初始化 */
static int fs_init_failed;          /* SD 卡初始化是否失败 */
static key_state_t prev_key;

/* 文件内容缓冲 */
static uint8_t content_buf[FS_FILE_MAX_SIZE];
static int content_len;
static int view_scroll;             /* 查看时的滚动偏移 */

/* 编辑缓冲 */
static char edit_buf[FS_FILE_MAX_SIZE];
static int edit_len;
static int grid_row, grid_col;      /* 字符网格光标位置 */
static int prev_grid_row, prev_grid_col;  /* 上次位置（局部刷新用） */

/* 重命名缓冲 */
static char name_buf[13];
static int name_len;
static int rename_error;            /* 重命名错误标志 */

/* OK 键长按检测(LIST 状态: 短按=打开文件, 长按=重命名) */
#define OK_LONG_PRESS_MS 600
static uint32_t ok_press_tick;
static uint8_t ok_was_pressed;

/* 删除/新建提示 */
static int msg_timer;               /* 提示消息倒计时 */
static char msg_text[24];

/* ---- 异步操作等待状态 ---- */
static file_op_type_t pending_op;   /* 正在等待的操作类型 */
static file_substate_t wait_return; /* 收到响应后返回到的子状态 */
static int wait_param;              /* 透传参数(如 sel_idx) */

/* ============================================================
 * 辅助函数
 * ============================================================ */

static void show_msg(const char *text)
{
    strncpy(msg_text, text, sizeof(msg_text) - 1);
    msg_text[sizeof(msg_text) - 1] = 0;
    msg_timer = 50;  /* 约 1 秒 */
}

/* 获取网格字符的显示标签 */
static void grid_label(int idx, char *label)
{
    if (idx == GRID_IDX_SPACE)
        strcpy(label, "SP");
    else if (idx == GRID_IDX_BACKSPACE)
        strcpy(label, "<-");
    else if (idx == GRID_IDX_SAVE)
        strcpy(label, "OK");
    else
    {
        label[0] = grid_chars[idx];
        label[1] = 0;
    }
}

/* 画单个网格格子
 * selected: 0=普通, 1=选中(黄底) */
static void draw_grid_cell(int row, int col, int selected)
{
    int idx = row * GRID_COLS + col;
    int x = GRID_X0 + col * GRID_CELL_W;
    int y = GRID_Y0 + row * GRID_CELL_H;
    char label[3];

    grid_label(idx, label);

    if (selected)
    {
        /* 选中：黄底黑字 */
        LCD_Fill(x, y, x + GRID_CELL_W - 2, y + GRID_CELL_H - 2, CLR_SEL);
        GUI_DrawString(x + 8, y + 10, label, BLACK, CLR_SEL, 1);
    }
    else
    {
        /* 未选中：黑底白字框 */
        LCD_Fill(x, y, x + GRID_CELL_W - 2, y + GRID_CELL_H - 2, CLR_BG);
        GUI_DrawRect(x, y, GRID_CELL_W - 2, GRID_CELL_H - 2, CLR_HINT);
        GUI_DrawString(x + 8, y + 10, label, CLR_TEXT, CLR_BG, 1);
    }
}

/* 局部刷新网格：只重绘旧位置和新位置两个格子 */
static void grid_partial_refresh(void)
{
    /* 旧位置变回普通态 */
    draw_grid_cell(prev_grid_row, prev_grid_col, 0);
    /* 新位置变选中态 */
    draw_grid_cell(grid_row, grid_col, 1);
    prev_grid_row = grid_row;
    prev_grid_col = grid_col;
}

/* 重绘内容预览行（编辑/重命名时） */
static void draw_preview_line(const char *title, const char *text, int text_len)
{
    (void)title;
    LCD_Fill(0, 35, SCR_W - 1, 80, CLR_BG);
    if (text_len > 0)
    {
        char preview[41];
        int show_len = text_len > 40 ? 40 : text_len;
        memcpy(preview, text, show_len);
        preview[show_len] = 0;
        GUI_DrawString(10, 45, preview, CLR_TEXT, CLR_BG, 1);
    }
    else
    {
        GUI_DrawString(10, 45, "(empty)", CLR_HINT, CLR_BG, 1);
    }
}

/* ============================================================
 * LIST 状态：文件列表
 * ============================================================ */

static void draw_list(void)
{
    int i, count;

    LCD_Clear(CLR_BG);

    /* 状态栏 */
    LCD_Fill(0, 0, SCR_W - 1, STATUS_H - 1, CLR_BG);
    if (fs_init_failed)
        GUI_DrawString(5, 8, "FILE MANAGER  [NO SD!]", CLR_WARN, CLR_BG, 1);
    else
        GUI_DrawString(5, 8, "FILE MANAGER", CLR_TITLE, CLR_BG, 1);

    count = FileSys_GetCount();
    {
        char buf[24];
        snprintf(buf, sizeof(buf), "[%d/%d]", count, FS_MAX_FILES);
        GUI_DrawString(SCR_W - 50, 8, buf, CLR_HINT, CLR_BG, 1);
    }

    /* 列表标题行 */
    GUI_DrawString(10, LIST_Y0 - 18, "Name         Size T St Blk", CLR_HINT, CLR_BG, 1);

    /* 文件列表 */
    if (count == 0)
    {
        GUI_DrawString(10, LIST_Y0 + 20, "No files.", CLR_HINT, CLR_BG, 1);
        GUI_DrawString(10, LIST_Y0 + 40, "Press [LEFT] to create.", CLR_BTN, CLR_BG, 1);
    }
    else
    {
        for (i = 0; i < LIST_ROWS && i + list_scroll < count; i++)
        {
            const file_entry_t *e = FileSys_GetEntry(i + list_scroll);
            int y = LIST_Y0 + i * LIST_ROW_H;
            char pretty[13];
            char line[40];
            char type_c;
            const char *state_s;

            if (e == NULL) break;

            FileSys_PrettyName(e->name, pretty);
            type_c  = (e->type == FS_TYPE_TEXT) ? 'T' :
                      (e->type == FS_TYPE_DRAW) ? 'D' : 'C';
            state_s = (e->state == FS_STATE_NORMAL)   ? "OK" :
                      (e->state == FS_STATE_READONLY) ? "RO" : "HD";

            snprintf(line, sizeof(line), "%s%-12s %3luB %c %s B%d",
                     (i + list_scroll == sel_idx) ? ">" : " ",
                     pretty, (unsigned long)e->size,
                     type_c, state_s, e->block_addr);

            GUI_DrawString(10, y, line,
                           (i + list_scroll == sel_idx) ? CLR_SEL : CLR_TEXT,
                           CLR_BG, 1);
        }
    }

    /* 底部操作提示（分两行，更清晰） */
    LCD_Fill(0, LIST_FOOTER_Y, SCR_W - 1, SCR_H - 1, CLR_BG);
    GUI_DrawString(5, LIST_FOOTER_Y,      "OK S:Open  OK L:Rename", CLR_HINT, CLR_BG, 1);
    GUI_DrawString(5, LIST_FOOTER_Y + 14, "UP/DN:Select  LEFT:New", CLR_HINT, CLR_BG, 1);
    GUI_DrawString(5, LIST_FOOTER_Y + 28, "RIGHT:Del  BACK:Exit", CLR_HINT, CLR_BG, 1);

    /* 提示消息 */
    if (msg_timer > 0)
    {
        GUI_DrawString(SCR_W - 100, LIST_FOOTER_Y, msg_text, CLR_WARN, CLR_BG, 1);
    }
}

static void run_list(key_state_t *key)
{
    uint8_t e_up    = (!prev_key.up)    && key->up;
    uint8_t e_down  = (!prev_key.down)  && key->down;
    uint8_t e_left  = (!prev_key.left)  && key->left;
    uint8_t e_right = (!prev_key.right) && key->right;
    uint8_t e_ok    = (!prev_key.ok)    && key->ok;
    uint8_t ok_released = (prev_key.ok) && (!key->ok);
    int count = FileSys_GetCount();

    /* 滚动选择 */
    if (e_down && sel_idx < count - 1)
    {
        sel_idx++;
        if (sel_idx >= list_scroll + LIST_ROWS)
            list_scroll++;
        draw_list();
    }
    if (e_up && sel_idx > 0)
    {
        sel_idx--;
        if (sel_idx < list_scroll)
            list_scroll--;
        draw_list();
    }

    /* ---- OK 长按检测: 短按=打开文件, 长按=重命名 ---- */
    if (e_ok)
    {
        ok_press_tick = xTaskGetTickCount();
        ok_was_pressed = 1;
    }

    /* 长按超过阈值: 立即触发重命名(不等释放) */
    if (ok_was_pressed && key->ok &&
        (xTaskGetTickCount() - ok_press_tick) * portTICK_PERIOD_MS >= OK_LONG_PRESS_MS)
    {
        ok_was_pressed = 0;  /* 消费, 防止释放时再触发短按 */
        if (count > 0)
        {
            const file_entry_t *e = FileSys_GetEntry(sel_idx);
            if (e)
            {
                /* 初始化 name_buf 为当前文件名, 进入 RENAME 状态 */
                strncpy(name_buf, e->name, 12);
                name_buf[12] = 0;
                name_len = strlen(name_buf);
                rename_error = 0;
                grid_row = 0;
                grid_col = 0;
                prev_grid_row = 0;
                prev_grid_col = 0;
                fs_state = FS_RENAME;
                need_redraw = 1;
            }
        }
    }

    /* OK 释放: 若未触发长按, 则为短按 = 打开文件 */
    if (ok_released && ok_was_pressed)
    {
        uint32_t hold_ms = (xTaskGetTickCount() - ok_press_tick) * portTICK_PERIOD_MS;
        ok_was_pressed = 0;

        if (hold_ms < OK_LONG_PRESS_MS && count > 0)
        {
            const file_entry_t *e = FileSys_GetEntry(sel_idx);

            /* 绘图文件: 触发 DRAW 应用打开 */
            if (e && e->type == FS_TYPE_DRAW)
            {
                app_draw_request_open();
                AppManager_GotoDesktop();
                prev_key = *key;
                return;
            }

            /* 文本文件: 异步投递 READ 请求 */
            {
                file_req_t req;
                memset(&req, 0, sizeof(req));
                req.op  = FILE_OP_READ;
                req.idx = sel_idx;
                req.buf = content_buf;
                req.len = FS_FILE_MAX_SIZE;

                if (FileTask_Request(&req, 0))
                {
                    view_scroll = 0;
                    pending_op = FILE_OP_READ;
                    wait_return = FS_VIEW;
                    wait_param = sel_idx;
                    fs_state = FS_WAIT;
                    need_redraw = 1;
                }
                else
                {
                    show_msg("BUSY!");
                    draw_list();
                }
            }
        }
    }

    /* LEFT: 新建文件 (异步: 投递请求后切到 FS_WAIT) */
    if (e_left)
    {
        if (count >= FS_MAX_FILES)
        {
            show_msg("FULL!");
            draw_list();
        }
        else
        {
            /* 自动命名: FILE01.TXT ~ FILE16.TXT */
            int i;
            for (i = 1; i <= FS_MAX_FILES; i++)
            {
                char name[13];
                snprintf(name, sizeof(name), "FILE%02d.TXT", i);
                if (!FileSys_NameExists(name))
                {
                    file_req_t req;
                    memset(&req, 0, sizeof(req));
                    req.op = FILE_OP_CREATE;
                    strncpy(req.name, name, sizeof(req.name) - 1);

                    if (FileTask_Request(&req, 0))
                    {
                        pending_op = FILE_OP_CREATE;
                        wait_return = FS_LIST;
                        fs_state = FS_WAIT;
                        need_redraw = 1;
                        LogStore_FileOp("Create", name);
                    }
                    else
                    {
                        show_msg("BUSY!");
                        draw_list();
                    }
                    break;
                }
            }
        }
    }

    /* RIGHT: 删除选中文件 */
    if (e_right && count > 0)
    {
        fs_state = FS_DELETE;
        need_redraw = 1;
    }

    /* 提示消息倒计时 */
    if (msg_timer > 0)
    {
        msg_timer--;
        if (msg_timer == 0)
            draw_list();
    }
}

/* ============================================================
 * VIEW 状态：查看文件内容
 *
 * 异步流程:
 *   1. 进入 VIEW 时投递 READ 请求,切到 FS_WAIT
 *   2. FS_WAIT 收到响应后 content_buf 已填充,调用 draw_view 绘制
 * ============================================================ */

static void draw_view(void)
{
    char pretty[13];
    char buf[32];
    int i, line_y;
    int visible_chars;

    LCD_Clear(CLR_BG);

    /* 状态栏 */
    const file_entry_t *e = FileSys_GetEntry(sel_idx);
    if (e == NULL)
    {
        fs_state = FS_LIST;
        need_redraw = 1;
        return;
    }

    FileSys_PrettyName(e->name, pretty);
    snprintf(buf, sizeof(buf), "VIEW: %s [%lu B]", pretty, (unsigned long)e->size);
    GUI_DrawString(5, 8, buf, CLR_TITLE, CLR_BG, 2);  /* scale=2 放大标题 */

    /* 内容显示区域 y=35~230 (content_buf 已由 FileTask 填充) */
    {
        int chars_per_line = 40;
        int line_h = 14;
        int max_lines = (230 - 35) / line_h;

        /* 逐行显示 */
        visible_chars = 0;
        line_y = 35;
        for (i = 0; i < content_len && line_y + line_h <= 230; i++)
        {
            int line_start = i;
            int line_len = 0;

            while (i < content_len && content_buf[i] != '\n' && line_len < chars_per_line)
            {
                line_len++;
                i++;
            }

            if (line_len > 0)
            {
                char line_buf[41];
                int skip = view_scroll * max_lines;

                if (visible_chars >= skip)
                {
                    memcpy(line_buf, content_buf + line_start, line_len);
                    line_buf[line_len] = 0;
                    GUI_DrawString(10, line_y, line_buf, CLR_TEXT, CLR_BG, 1);
                    line_y += line_h;
                }
                visible_chars++;
            }

            if (i < content_len && content_buf[i] == '\n')
                continue;
        }

        if (content_len == 0)
            GUI_DrawString(10, 35, "(empty)", CLR_HINT, CLR_BG, 1);
    }

    /* 底部提示 */
    LCD_Fill(0, LIST_FOOTER_Y, SCR_W - 1, SCR_H - 1, CLR_BG);
    GUI_DrawString(5, LIST_FOOTER_Y,      "Viewing content (read-only)", CLR_HINT, CLR_BG, 1);
    GUI_DrawString(5, LIST_FOOTER_Y + 14, "OK:Edit  UP/DN:Scroll", CLR_BTN, CLR_BG, 1);
    GUI_DrawString(5, LIST_FOOTER_Y + 28, "BACK:Back to list", CLR_HINT, CLR_BG, 1);
}

static void run_view(key_state_t *key)
{
    uint8_t e_up   = (!prev_key.up)   && key->up;
    uint8_t e_down = (!prev_key.down) && key->down;
    uint8_t e_ok   = (!prev_key.ok)   && key->ok;

    if (e_up && view_scroll > 0)
    {
        view_scroll--;
        draw_view();
    }
    if (e_down)
    {
        view_scroll++;
        draw_view();
    }

    if (e_ok)
    {
        /* 进入编辑模式，复制内容到编辑缓冲 */
        fs_state = FS_EDIT;
        memcpy(edit_buf, content_buf, content_len);
        edit_len = content_len;
        grid_row = 0;
        grid_col = 0;
        prev_grid_row = 0;
        prev_grid_col = 0;
        need_redraw = 1;
    }
}

/* ============================================================
 * EDIT / RENAME 状态：字符网格输入
 * ============================================================ */

static void draw_grid_full(const char *title, const char *text, int text_len)
{
    int r, c;

    LCD_Clear(CLR_BG);

    /* 状态栏 */
    GUI_DrawString(5, 8, title, CLR_TITLE, CLR_BG, 1);

    /* 内容预览 y=35~80 */
    draw_preview_line(title, text, text_len);

    /* 字符网格 y=90~210 */
    for (r = 0; r < GRID_ROWS; r++)
    {
        for (c = 0; c < GRID_COLS; c++)
        {
            draw_grid_cell(r, c, (r == grid_row && c == grid_col));
        }
    }

    /* 底部提示 */
    LCD_Fill(0, LIST_FOOTER_Y, SCR_W - 1, SCR_H - 1, CLR_BG);
    GUI_DrawString(5, LIST_FOOTER_Y,      "UP/DN/LF/RT:Move  OK:Pick", CLR_HINT, CLR_BG, 1);
    GUI_DrawString(5, LIST_FOOTER_Y + 14, "Pick 'OK' cell = Save", CLR_HINT, CLR_BG, 1);
    GUI_DrawString(5, LIST_FOOTER_Y + 28, "BACK:Cancel", CLR_HINT, CLR_BG, 1);

    /* 错误提示 */
    if (rename_error)
    {
        GUI_DrawString(SCR_W - 120, LIST_FOOTER_Y, "NAME EXISTS!", CLR_WARN, CLR_BG, 1);
    }

    prev_grid_row = grid_row;
    prev_grid_col = grid_col;
}

static void run_edit(key_state_t *key)
{
    uint8_t e_up    = (!prev_key.up)    && key->up;
    uint8_t e_down  = (!prev_key.down)  && key->down;
    uint8_t e_left  = (!prev_key.left)  && key->left;
    uint8_t e_right = (!prev_key.right) && key->right;
    uint8_t e_ok    = (!prev_key.ok)    && key->ok;

    /* 光标移动：只触发局部刷新 */
    if (e_left && grid_col > 0)  { grid_col--; need_grid_partial = 1; }
    if (e_right && grid_col < GRID_COLS - 1) { grid_col++; need_grid_partial = 1; }
    if (e_up && grid_row > 0)    { grid_row--; need_grid_partial = 1; }
    if (e_down && grid_row < GRID_ROWS - 1)  { grid_row++; need_grid_partial = 1; }

    if (e_ok)
    {
        int idx = grid_row * GRID_COLS + grid_col;

        if (idx == GRID_IDX_SAVE)
        {
            /* 保存: 异步写入 SD 卡 */
            file_req_t req;
            memset(&req, 0, sizeof(req));
            req.op  = FILE_OP_WRITE;
            req.idx = sel_idx;
            req.buf = (uint8_t *)edit_buf;
            req.len = edit_len;

            if (FileTask_Request(&req, 0))
            {
                pending_op = FILE_OP_WRITE;
                wait_return = FS_VIEW;  /* 写完后重新读以刷新 content_buf */
                wait_param = sel_idx;
                fs_state = FS_WAIT;
                need_redraw = 1;
                LogStore_FileOp("Write", "file");
            }
            else
            {
                show_msg("BUSY!");
                need_redraw = 1;
            }
        }
        else if (idx == GRID_IDX_BACKSPACE)
        {
            if (edit_len > 0) edit_len--;
            /* 只刷新预览行，不重绘网格 */
            draw_preview_line("EDIT MODE", edit_buf, edit_len);
        }
        else if (edit_len < FS_FILE_MAX_SIZE - 1)
        {
            char ch = grid_chars[idx];
            edit_buf[edit_len++] = ch;
            draw_preview_line("EDIT MODE", edit_buf, edit_len);
        }
    }
}

static void run_rename(key_state_t *key)
{
    uint8_t e_up    = (!prev_key.up)    && key->up;
    uint8_t e_down  = (!prev_key.down)  && key->down;
    uint8_t e_left  = (!prev_key.left)  && key->left;
    uint8_t e_right = (!prev_key.right) && key->right;
    uint8_t e_ok    = (!prev_key.ok)    && key->ok;

    rename_error = 0;

    /* 光标移动 */
    if (e_left && grid_col > 0)  { grid_col--; need_grid_partial = 1; }
    if (e_right && grid_col < GRID_COLS - 1) { grid_col++; need_grid_partial = 1; }
    if (e_up && grid_row > 0)    { grid_row--; need_grid_partial = 1; }
    if (e_down && grid_row < GRID_ROWS - 1)  { grid_row++; need_grid_partial = 1; }

    if (e_ok)
    {
        int idx = grid_row * GRID_COLS + grid_col;

        if (idx == GRID_IDX_SAVE)
        {
            /* 保存: 异步重命名 */
            file_req_t req;
            name_buf[name_len] = 0;
            memset(&req, 0, sizeof(req));
            req.op  = FILE_OP_RENAME;
            req.idx = sel_idx;
            strncpy(req.name, name_buf, sizeof(req.name) - 1);

            if (FileTask_Request(&req, 0))
            {
                pending_op = FILE_OP_RENAME;
                wait_return = FS_RENAME;  /* 留在 RENAME 状态等待结果(可能重名) */
                wait_param = sel_idx;
                fs_state = FS_WAIT;
                need_redraw = 1;
            }
            else
            {
                show_msg("BUSY!");
                need_redraw = 1;
            }
        }
        else if (idx == GRID_IDX_BACKSPACE)
        {
            if (name_len > 0) name_len--;
            draw_preview_line("RENAME MODE", name_buf, name_len);
        }
        else if (name_len < 12)
        {
            char ch = grid_chars[idx];
            name_buf[name_len++] = ch;
            draw_preview_line("RENAME MODE", name_buf, name_len);
        }
    }
}

/* ============================================================
 * DELETE 状态：删除确认
 * ============================================================ */

static void draw_delete(void)
{
    char pretty[13];
    char buf[32];
    const file_entry_t *e = FileSys_GetEntry(sel_idx);

    LCD_Clear(CLR_BG);
    GUI_DrawString(5, 8, "DELETE FILE", CLR_WARN, CLR_BG, 1);

    if (e)
    {
        FileSys_PrettyName(e->name, pretty);
        snprintf(buf, sizeof(buf), "Delete %s?", pretty);
        GUI_DrawString(10, 120, buf, CLR_TEXT, CLR_BG, 2);
    }

    GUI_DrawString(10, 200, "[OK] Confirm  [BACK] Cancel", CLR_HINT, CLR_BG, 1);
}

static void run_delete(key_state_t *key)
{
    uint8_t e_ok   = (!prev_key.ok)   && key->ok;
    uint8_t e_back = (!prev_key.back) && key->back;

    if (e_ok)
    {
        /* 异步删除 */
        file_req_t req;
        char pretty[13];
        const file_entry_t *e = FileSys_GetEntry(sel_idx);
        if (e)
        {
            FileSys_PrettyName(e->name, pretty);
            LogStore_FileOp("Delete", pretty);
        }

        memset(&req, 0, sizeof(req));
        req.op  = FILE_OP_DELETE;
        req.idx = sel_idx;

        if (FileTask_Request(&req, 0))
        {
            pending_op = FILE_OP_DELETE;
            wait_return = FS_LIST;
            wait_param = sel_idx;
            fs_state = FS_WAIT;
            need_redraw = 1;
        }
        else
        {
            show_msg("BUSY!");
            need_redraw = 1;
        }
    }

    if (e_back)
    {
        fs_state = FS_LIST;
        need_redraw = 1;
    }
}

/* ============================================================
 * 应用入口
 * ============================================================ */

void app_file_create(void)
{
    /* FileSys_Init 已在 InputTask 启动时执行,这里只检查结果 */
    if (!fs_initialized)
    {
        if (!FileSys_IsReady())
            fs_init_failed = 1;
        fs_initialized = 1;
    }
}

void app_file_start(void)
{
    need_redraw = 1;
}

/* ---- FS_WAIT 状态: 非阻塞检查后台文件操作响应 ---- */
static void run_wait(void)
{
    file_resp_t resp;

    /* 非阻塞取响应 */
    if (!FileTask_GetResponse(&resp, 0))
        return;  /* 还没完成,继续等 */

    /* 收到响应,根据操作类型处理 */
    switch (resp.op)
    {
        case FILE_OP_READ:
            content_len = (resp.result >= 0) ? resp.result : 0;
            fs_state = FS_VIEW;
            need_redraw = 1;
            break;

        case FILE_OP_WRITE:
            /* 写入完成,重新投递 READ 请求刷新 content_buf */
            {
                file_req_t req;
                memset(&req, 0, sizeof(req));
                req.op  = FILE_OP_READ;
                req.idx = wait_param;
                req.buf = content_buf;
                req.len = FS_FILE_MAX_SIZE;
                if (FileTask_Request(&req, 0))
                {
                    pending_op = FILE_OP_READ;
                    wait_return = FS_VIEW;
                    fs_state = FS_WAIT;
                    need_redraw = 1;
                }
                else
                {
                    /* 投递失败,直接切回 VIEW(用旧数据) */
                    fs_state = FS_VIEW;
                    need_redraw = 1;
                }
            }
            break;

        case FILE_OP_CREATE:
            if (resp.result >= 0)
            {
                sel_idx = FileSys_GetCount() - 1;
                show_msg("CREATED");
            }
            else
            {
                show_msg("FAIL!");
            }
            fs_state = FS_LIST;
            need_redraw = 1;
            break;

        case FILE_OP_DELETE:
            if (sel_idx > 0) sel_idx--;
            show_msg("DELETED");
            fs_state = FS_LIST;
            need_redraw = 1;
            break;

        case FILE_OP_RENAME:
            if (resp.result == -2)
            {
                /* 重名 */
                rename_error = 1;
                fs_state = FS_RENAME;
            }
            else
            {
                fs_state = FS_LIST;
            }
            need_redraw = 1;
            break;

        default:
            /* 异常,回到列表 */
            fs_state = FS_LIST;
            need_redraw = 1;
            break;
    }
}

/* ---- 绘制等待界面 ---- */
static void draw_wait(void)
{
    const char *op_str;
    LCD_Clear(CLR_BG);
    switch (pending_op)
    {
        case FILE_OP_READ:   op_str = "Reading...";   break;
        case FILE_OP_WRITE:  op_str = "Writing...";   break;
        case FILE_OP_CREATE: op_str = "Creating...";  break;
        case FILE_OP_DELETE: op_str = "Deleting...";  break;
        case FILE_OP_RENAME: op_str = "Renaming...";  break;
        default:             op_str = "Processing..."; break;
    }
    GUI_DrawString(120, 140, op_str, YELLOW, BLACK, 2);
    GUI_DrawString(100, 180, "Please wait", CLR_HINT, BLACK, 1);
}

void app_file_run(key_state_t *key)
{
    /* 输入事件计数 */
    if (key->ok || key->up || key->down || key->left || key->right)
        Monitor_IncInputEvent();

    /* ---- FS_WAIT 优先处理(非阻塞轮询响应) ---- */
    if (fs_state == FS_WAIT)
    {
        run_wait();
        /* run_wait 可能改 fs_state,下面统一处理重绘 */
    }

    /* 全屏重绘（首次进入/状态切换） */
    if (need_redraw)
    {
        switch (fs_state)
        {
            case FS_LIST:   draw_list(); break;
            case FS_VIEW:   draw_view(); break;
            case FS_EDIT:   draw_grid_full("EDIT MODE", edit_buf, edit_len); break;
            case FS_RENAME: draw_grid_full("RENAME MODE", name_buf, name_len); break;
            case FS_DELETE: draw_delete(); break;
            case FS_WAIT:   draw_wait(); break;
        }
        need_redraw = 0;
        need_grid_partial = 0;
        prev_key = *key;
        return;
    }

    /* 网格局部刷新（光标移动） */
    if (need_grid_partial)
    {
        grid_partial_refresh();
        need_grid_partial = 0;
    }

    /* ---- BACK 键：分层返回 ----
     * LIST   → 退出到桌面
     * VIEW   → 返回列表
     * EDIT   → 取消编辑，返回查看
     * RENAME → 取消重命名，返回列表
     * DELETE → 取消删除，返回列表
     * WAIT   → 不响应(操作进行中) */
    {
        uint8_t e_back = (!prev_key.back) && key->back;
        if (e_back)
        {
            switch (fs_state)
            {
                case FS_LIST:
                    AppManager_GotoDesktop();
                    prev_key = *key;
                    return;
                case FS_VIEW:
                    fs_state = FS_LIST;
                    need_redraw = 1;
                    prev_key = *key;
                    return;
                case FS_EDIT:
                    fs_state = FS_VIEW;
                    need_redraw = 1;
                    prev_key = *key;
                    return;
                case FS_RENAME:
                    fs_state = FS_LIST;
                    need_redraw = 1;
                    prev_key = *key;
                    return;
                case FS_DELETE:
                    fs_state = FS_LIST;
                    need_redraw = 1;
                    prev_key = *key;
                    return;
                /* FS_WAIT 不响应 BACK */
                default: break;
            }
        }
    }

    /* 分发到子状态 */
    switch (fs_state)
    {
        case FS_LIST:   run_list(key); break;
        case FS_VIEW:   run_view(key); break;
        case FS_EDIT:   run_edit(key); break;
        case FS_RENAME: run_rename(key); break;
        case FS_DELETE: run_delete(key); break;
        /* FS_WAIT 已在上面处理 */
        default: break;
    }

    prev_key = *key;
}

void app_file_pause(void)
{
    need_redraw = 1;
}
