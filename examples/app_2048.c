/**
 ******************************************************************************
 * @file    App.c
 * @brief   2048 小游戏（240x320 竖屏，触摸滑动操作）
 *
 *          操作：在屏幕上滑动 —— 上/下/左/右，相同数字自动合并。
 *          手感：滑过阈值立刻响应，不等松手；方块滑行带缓出、
 *                合并闪光、新方块弹出。
 *          游戏结束后轻点屏幕重开；按住不动 3 秒可重新校准触摸。
 *
 *          需要 lcd.h 里 LCD_USE_TOUCH = 1。
 ******************************************************************************
 */

#include "app.h"
#include "lcd.h"

#if (LCD_USE_TOUCH == 0)

/* 没有触摸就玩不了，给个提示 */
void App_Init(void)
{
    LCD_Init();
    LCD_SetDir(LCD_DIR_0);
    LCD_Clear(LCD_WHITE);
    LCD_ShowString(10, 10, "Enable LCD_USE_TOUCH", LCD_RED, LCD_WHITE, 1);
    LCD_ShowString(10, 30, "in lcd.h to play 2048", LCD_BLUE, LCD_WHITE, 1);
}

void App_Run(void)
{
}

#else

/* ---------------- 布局 ---------------- */
#define GRID_SIZE       4
#define CELL_SIZE       51
#define CELL_GAP        6
#define BOARD_X         9
#define BOARD_Y         56
#define BOARD_H         (GRID_SIZE * (CELL_SIZE + CELL_GAP))
#define HEADER_H        52
#define MAX_TILES       (GRID_SIZE * GRID_SIZE)

/* ---------------- 动画参数 ---------------- */
#define ANIM_MS         130U        /* 滑行时长 */
#define POP_STEP_MS     22U         /* 新方块每一帧停留 */
#define MERGE_FLASH_MS  45U         /* 合并闪光时长 */

/* ---------------- 配色 ---------------- */
#define C_BOARD         0xBD74
#define C_EMPTY         0xCE16
#define C_FLASH         0xFFFF
#define C_TEXT_DARK     0x0000
#define C_TEXT_LIGHT    0xFFFF

/* ---------------- 类型 ---------------- */
typedef struct
{
    uint8_t  fr;
    uint8_t  fc;
    uint8_t  tr;
    uint8_t  tc;
    uint16_t value;
    uint16_t px;        /* 上一帧画到的位置，用于擦除 */
    uint16_t py;
} anim_tile_t;

/* ---------------- 游戏状态 ---------------- */
static uint16_t g_board[GRID_SIZE][GRID_SIZE];
static uint32_t g_score;
static uint32_t g_best;
static uint8_t  g_over;
static uint32_t g_rng;

/* 动画数据 */
static anim_tile_t g_anim[MAX_TILES];
static uint8_t     g_anim_count;
static uint8_t     g_merge_cells[MAX_TILES];
static uint8_t     g_merge_count;

/* 触摸状态 */
static uint8_t  g_touching;
static uint8_t  g_swipe_done;
static uint16_t g_start_x;
static uint16_t g_start_y;
static uint16_t g_last_x;
static uint16_t g_last_y;
static uint32_t g_start_tick;

/*==============================================================================
 * 工具
 *============================================================================*/
static uint32_t rng_next(void)
{
    g_rng = g_rng * 1664525UL + 1013904223UL;
    return (g_rng >> 16);
}

static uint16_t tile_color(uint16_t v)
{
    switch (v)
    {
        case 2:    return 0xEF3B;
        case 4:    return 0xEF19;
        case 8:    return 0xF58F;
        case 16:   return 0xF4AC;
        case 32:   return 0xF3EB;
        case 64:   return 0xF2E7;
        case 128:  return 0xEE6E;
        case 256:  return 0xEE6C;
        case 512:  return 0xEE4A;
        case 1024: return 0xEE27;
        case 2048: return 0xEE05;
        default:   return 0xE000;
    }
}

static uint16_t cell_x(uint8_t col)
{
    return (uint16_t)(BOARD_X + col * (CELL_SIZE + CELL_GAP));
}

static uint16_t cell_y(uint8_t row)
{
    return (uint16_t)(BOARD_Y + row * (CELL_SIZE + CELL_GAP));
}

static uint8_t dist_of(uint16_t a, uint16_t b)
{
    uint16_t d = (a > b) ? (uint16_t)(a - b) : (uint16_t)(b - a);

    return (uint8_t)((d > 255U) ? 255U : d);
}

/* 在矩形内居中显示数字，位数多时自动缩小字号 */
static void draw_num_centered(uint16_t x, uint16_t y, uint16_t w, uint16_t h,
                              uint32_t value, uint16_t fg, uint16_t bg)
{
    uint8_t  digits = 1;
    uint8_t  size;
    uint32_t t = value;
    uint16_t tw;
    uint16_t th;

    while (t >= 10U)
    {
        t /= 10U;
        digits++;
    }

    size = (digits <= 2U) ? 2U : 1U;
    tw   = (uint16_t)(digits * 8U * size);
    th   = (uint16_t)(16U * size);

    LCD_ShowNum((uint16_t)(x + ((w - tw) / 2U)),
                (uint16_t)(y + ((h - th) / 2U)),
                value, digits, fg, bg, size);
}

/*==============================================================================
 * 绘制
 *============================================================================*/
static void draw_header(void)
{
    LCD_Fill(0, 0, LCD_PANEL_WIDTH, HEADER_H, C_BOARD);

    LCD_ShowString(8, 0, "SCORE", C_TEXT_LIGHT, C_BOARD, 1);
    LCD_ShowNum(8, 16, g_score, 1, C_TEXT_LIGHT, C_BOARD, 2);

    LCD_ShowString(140, 0, "BEST", C_TEXT_LIGHT, C_BOARD, 1);
    LCD_ShowNum(140, 16, g_best, 1, C_TEXT_LIGHT, C_BOARD, 2);

    LCD_ShowString(104, 0, "2048", 0xFFE0, C_BOARD, 1);
}

static void draw_footer(void)
{
    LCD_Fill(0, 282, LCD_PANEL_WIDTH, 38, C_BOARD);
    LCD_ShowString(64, 292, "swipe to move", C_TEXT_LIGHT, C_BOARD, 1);
}

static void draw_cell(uint8_t row, uint8_t col)
{
    uint16_t v  = g_board[row][col];
    uint16_t bg = (v != 0U) ? tile_color(v) : C_EMPTY;
    uint16_t fg = (v <= 4U) ? C_TEXT_DARK : C_TEXT_LIGHT;
    uint16_t x  = cell_x(col);
    uint16_t y  = cell_y(row);

    LCD_Fill(x, y, CELL_SIZE, CELL_SIZE, bg);

    if (v != 0U)
    {
        draw_num_centered(x, y, CELL_SIZE, CELL_SIZE, v, fg, bg);
    }
}

static void draw_board(void)
{
    uint8_t r;
    uint8_t c;

    LCD_Fill(0, (uint16_t)(BOARD_Y - CELL_GAP), LCD_PANEL_WIDTH, BOARD_H, C_BOARD);

    for (r = 0; r < GRID_SIZE; r++)
    {
        for (c = 0; c < GRID_SIZE; c++)
        {
            draw_cell(r, c);
        }
    }
}

static void draw_overlay(void)
{
    LCD_Fill(24, 128, 192, 88, 0x0000);

    LCD_ShowString(52, 138, "GAME OVER", 0xFD20, 0x0000, 2);
    LCD_ShowString(56, 172, "score", C_TEXT_LIGHT, 0x0000, 1);
    LCD_ShowNum(104, 172, g_score, 1, 0xFD20, 0x0000, 1);
    LCD_ShowString(40, 192, "TAP TO RESTART", C_TEXT_LIGHT, 0x0000, 1);
}

/*==============================================================================
 * 动画
 *============================================================================*/
/* 用“棋盘底色 + 空格子色”重画一块区域。
 * 只擦移动方块走过的位置，不整屏清，避免闪屏。 */
static void paint_bg_region(uint16_t x, uint16_t y, uint16_t w, uint16_t h)
{
    uint16_t x2 = (uint16_t)(x + w);
    uint16_t y2 = (uint16_t)(y + h);
    uint8_t  r;
    uint8_t  c;

    LCD_Fill(x, y, w, h, C_BOARD);

    for (r = 0; r < GRID_SIZE; r++)
    {
        for (c = 0; c < GRID_SIZE; c++)
        {
            uint16_t cx  = cell_x(c);
            uint16_t cy  = cell_y(r);
            uint16_t ix;
            uint16_t iy;
            uint16_t ix2;
            uint16_t iy2;

            if ((cx >= x2) || ((uint16_t)(cx + CELL_SIZE) <= x) ||
                (cy >= y2) || ((uint16_t)(cy + CELL_SIZE) <= y))
            {
                continue;
            }

            ix  = (cx > x) ? cx : x;
            iy  = (cy > y) ? cy : y;
            ix2 = ((uint16_t)(cx + CELL_SIZE) < x2) ? (uint16_t)(cx + CELL_SIZE) : x2;
            iy2 = ((uint16_t)(cy + CELL_SIZE) < y2) ? (uint16_t)(cy + CELL_SIZE) : y2;

            if ((ix2 > ix) && (iy2 > iy))
            {
                LCD_Fill(ix, iy, (uint16_t)(ix2 - ix), (uint16_t)(iy2 - iy), C_EMPTY);
            }
        }
    }
}

/* 按缓出曲线把方块从起点滑到终点，期间不画数字，保证帧率。
 * 每帧只擦掉移动方块上一帧的矩形，静止的方块完全不重绘。 */
static void animate_slide(void)
{
    uint32_t start;
    uint32_t now;
    uint32_t u;
    uint32_t e;
    uint8_t  i;

    if (g_anim_count == 0U)
    {
        return;
    }

    /* 记录初始位置 */
    for (i = 0; i < g_anim_count; i++)
    {
        g_anim[i].px = cell_x(g_anim[i].fc);
        g_anim[i].py = cell_y(g_anim[i].fr);
    }

    start = HAL_GetTick();

    for (;;)
    {
        now = HAL_GetTick();
        u   = ((now - start) * 100U) / ANIM_MS;

        if (u > 100U)
        {
            u = 100U;
        }

        /* ease-out：e = 100 - (100 - u)^2 / 100 */
        e = 100U - (((100U - u) * (100U - u)) / 100U);

        /* 1. 先擦掉所有移动方块上一帧的位置 */
        for (i = 0; i < g_anim_count; i++)
        {
            if ((g_anim[i].fr == g_anim[i].tr) && (g_anim[i].fc == g_anim[i].tc))
            {
                continue;               /* 不动的方块不用管 */
            }

            paint_bg_region(g_anim[i].px, g_anim[i].py, CELL_SIZE, CELL_SIZE);
        }

        /* 2. 静止的方块补画一遍（擦除时可能被误伤） */
        for (i = 0; i < g_anim_count; i++)
        {
            if ((g_anim[i].fr != g_anim[i].tr) || (g_anim[i].fc != g_anim[i].tc))
            {
                continue;
            }

            LCD_Fill(cell_x(g_anim[i].fc), cell_y(g_anim[i].fr),
                     CELL_SIZE, CELL_SIZE, tile_color(g_anim[i].value));
        }

        /* 3. 移动的方块画到新位置 */
        for (i = 0; i < g_anim_count; i++)
        {
            int32_t x0;
            int32_t y0;
            int32_t x1;
            int32_t y1;
            int32_t x;
            int32_t y;

            if ((g_anim[i].fr == g_anim[i].tr) && (g_anim[i].fc == g_anim[i].tc))
            {
                continue;
            }

            x0 = (int32_t)cell_x(g_anim[i].fc);
            y0 = (int32_t)cell_y(g_anim[i].fr);
            x1 = (int32_t)cell_x(g_anim[i].tc);
            y1 = (int32_t)cell_y(g_anim[i].tr);
            x  = x0 + (((x1 - x0) * (int32_t)e) / 100);
            y  = y0 + (((y1 - y0) * (int32_t)e) / 100);

            LCD_Fill((uint16_t)x, (uint16_t)y, CELL_SIZE, CELL_SIZE,
                     tile_color(g_anim[i].value));

            g_anim[i].px = (uint16_t)x;
            g_anim[i].py = (uint16_t)y;
        }

        if (u >= 100U)
        {
            break;
        }
    }
}

/* 合并闪光：先把合并的格子刷亮，再恢复 */
static void flash_merged(void)
{
    uint8_t i;
    uint8_t idx;

    if (g_merge_count == 0U)
    {
        return;
    }

    for (i = 0; i < g_merge_count; i++)
    {
        idx = g_merge_cells[i];
        LCD_Fill(cell_x((uint8_t)(idx % GRID_SIZE)),
                 cell_y((uint8_t)(idx / GRID_SIZE)),
                 CELL_SIZE, CELL_SIZE, C_FLASH);
    }

    HAL_Delay(MERGE_FLASH_MS);

    for (i = 0; i < g_merge_count; i++)
    {
        idx = g_merge_cells[i];
        draw_cell((uint8_t)(idx / GRID_SIZE), (uint8_t)(idx % GRID_SIZE));
    }
}

/* 新方块弹出：从小放大三帧 */
static void pop_cell(uint8_t row, uint8_t col)
{
    static const uint8_t pct[3] = {55U, 78U, 100U};
    uint16_t v = g_board[row][col];
    uint16_t x = cell_x(col);
    uint16_t y = cell_y(row);
    uint8_t  i;

    for (i = 0; i < 3U; i++)
    {
        uint16_t w   = (uint16_t)((CELL_SIZE * pct[i]) / 100U);
        uint16_t off = (uint16_t)((CELL_SIZE - w) / 2U);

        LCD_Fill(x, y, CELL_SIZE, CELL_SIZE, C_EMPTY);
        LCD_Fill((uint16_t)(x + off), (uint16_t)(y + off), w, w, tile_color(v));
        HAL_Delay(POP_STEP_MS);
    }

    draw_cell(row, col);
}

/*==============================================================================
 * 游戏逻辑
 *============================================================================*/
static void spawn_tile(uint8_t *out_row, uint8_t *out_col)
{
    uint8_t empty[MAX_TILES];
    uint8_t count = 0;
    uint8_t r;
    uint8_t c;
    uint8_t i;

    for (r = 0; r < GRID_SIZE; r++)
    {
        for (c = 0; c < GRID_SIZE; c++)
        {
            if (g_board[r][c] == 0U)
            {
                empty[count++] = (uint8_t)(r * GRID_SIZE + c);
            }
        }
    }

    if (count == 0U)
    {
        return;
    }

    i = (uint8_t)(rng_next() % count);
    r = (uint8_t)(empty[i] / GRID_SIZE);
    c = (uint8_t)(empty[i] % GRID_SIZE);

    g_board[r][c] = ((rng_next() % 10U) < 9U) ? 2U : 4U;

    *out_row = r;
    *out_col = c;
}

/* 压缩合并一行，同时记录每个源方块的目标位置和合并位置 */
static uint8_t slide_line(uint16_t *line, uint32_t *gain, int8_t *dest, uint8_t *merged)
{
    uint16_t out[GRID_SIZE] = {0, 0, 0, 0};
    uint8_t  w = 0;
    uint8_t  merged_last = 0;
    uint8_t  changed = 0;
    uint8_t  i;

    for (i = 0; i < GRID_SIZE; i++)
    {
        dest[i]   = -1;
        merged[i] = 0;
    }

    for (i = 0; i < GRID_SIZE; i++)
    {
        uint16_t v = line[i];

        if (v == 0U)
        {
            continue;
        }

        if ((w > 0U) && (out[w - 1U] == v) && (merged_last == 0U))
        {
            out[w - 1U]    = (uint16_t)(v * 2U);
            *gain         += out[w - 1U];
            dest[i]        = (int8_t)(w - 1U);
            merged[w - 1U] = 1U;
            merged_last    = 1U;
        }
        else
        {
            out[w]      = v;
            dest[i]     = (int8_t)w;
            w++;
            merged_last = 0U;
        }
    }

    for (i = 0; i < GRID_SIZE; i++)
    {
        if (line[i] != out[i])
        {
            changed = 1U;
        }
        line[i] = out[i];
    }

    return changed;
}

/* dir: 0=左 1=右 2=上 3=下；返回 1 表示棋盘有变化 */
static uint8_t compute_move(uint8_t dir)
{
    uint16_t line[GRID_SIZE];
    uint16_t src[GRID_SIZE];
    int8_t   dest[GRID_SIZE];
    uint8_t  merged[GRID_SIZE];
    uint32_t gain = 0;
    uint8_t  changed = 0;
    uint8_t  i;
    uint8_t  j;
    uint8_t  k;

    g_anim_count  = 0;
    g_merge_count = 0;

    for (i = 0; i < GRID_SIZE; i++)
    {
        for (j = 0; j < GRID_SIZE; j++)
        {
            switch (dir)
            {
                case 0:  line[j] = g_board[i][j]; break;
                case 1:  line[j] = g_board[i][GRID_SIZE - 1U - j]; break;
                case 2:  line[j] = g_board[j][i]; break;
                default: line[j] = g_board[GRID_SIZE - 1U - j][i]; break;
            }
            src[j] = line[j];
        }

        if (slide_line(line, &gain, dest, merged) != 0U)
        {
            changed = 1U;
        }

        /* 收集动画：每个非空源方块从原格滑到目标格 */
        for (j = 0; j < GRID_SIZE; j++)
        {
            uint8_t fr;
            uint8_t fc;
            uint8_t tr;
            uint8_t tc;

            if ((src[j] == 0U) || (dest[j] < 0))
            {
                continue;
            }

            switch (dir)
            {
                case 0:  fr = i; fc = j; break;
                case 1:  fr = i; fc = (uint8_t)(GRID_SIZE - 1U - j); break;
                case 2:  fr = j; fc = i; break;
                default: fr = (uint8_t)(GRID_SIZE - 1U - j); fc = i; break;
            }

            switch (dir)
            {
                case 0:  tr = i; tc = (uint8_t)dest[j]; break;
                case 1:  tr = i; tc = (uint8_t)(GRID_SIZE - 1U - dest[j]); break;
                case 2:  tr = (uint8_t)dest[j]; tc = i; break;
                default: tr = (uint8_t)(GRID_SIZE - 1U - dest[j]); tc = i; break;
            }

            g_anim[g_anim_count].fr    = fr;
            g_anim[g_anim_count].fc    = fc;
            g_anim[g_anim_count].tr    = tr;
            g_anim[g_anim_count].tc    = tc;
            g_anim[g_anim_count].value = src[j];
            g_anim_count++;
        }

        /* 收集合并位置，用于闪光 */
        for (k = 0; k < GRID_SIZE; k++)
        {
            uint8_t mr;
            uint8_t mc;

            if (merged[k] == 0U)
            {
                continue;
            }

            switch (dir)
            {
                case 0:  mr = i; mc = k; break;
                case 1:  mr = i; mc = (uint8_t)(GRID_SIZE - 1U - k); break;
                case 2:  mr = k; mc = i; break;
                default: mr = (uint8_t)(GRID_SIZE - 1U - k); mc = i; break;
            }

            g_merge_cells[g_merge_count++] = (uint8_t)(mr * GRID_SIZE + mc);
        }

        /* 写回棋盘 */
        for (k = 0; k < GRID_SIZE; k++)
        {
            switch (dir)
            {
                case 0:  g_board[i][k] = line[k]; break;
                case 1:  g_board[i][GRID_SIZE - 1U - k] = line[k]; break;
                case 2:  g_board[k][i] = line[k]; break;
                default: g_board[GRID_SIZE - 1U - k][i] = line[k]; break;
            }
        }
    }

    g_score += gain;

    return changed;
}

static uint8_t board_full(void)
{
    uint8_t r;
    uint8_t c;

    for (r = 0; r < GRID_SIZE; r++)
    {
        for (c = 0; c < GRID_SIZE; c++)
        {
            if (g_board[r][c] == 0U)
            {
                return 0;
            }

            if (((c + 1U) < GRID_SIZE) && (g_board[r][c] == g_board[r][c + 1U]))
            {
                return 0;
            }

            if (((r + 1U) < GRID_SIZE) && (g_board[r][c] == g_board[r + 1U][c]))
            {
                return 0;
            }
        }
    }

    return 1;
}

static void restart_game(void)
{
    uint8_t r;
    uint8_t c;
    uint8_t nr;
    uint8_t nc;

    for (r = 0; r < GRID_SIZE; r++)
    {
        for (c = 0; c < GRID_SIZE; c++)
        {
            g_board[r][c] = 0;
        }
    }

    g_score = 0;
    g_over  = 0;
    g_rng   = HAL_GetTick() | 1U;

    spawn_tile(&nr, &nc);
    spawn_tile(&nr, &nc);

    draw_header();
    draw_board();
    draw_footer();
}

/* 执行一步移动：滑行 -> 定格 -> 合并闪光 -> 新方块弹出 */
static void do_move(uint8_t dir)
{
    uint8_t moved;
    uint8_t nr = 0;
    uint8_t nc = 0;

    moved = compute_move(dir);

    if (moved == 0U)
    {
        return;
    }

    animate_slide();        /* 1. 方块滑行 */
    draw_board();           /* 2. 定格并画出数字 */
    flash_merged();         /* 3. 合并闪光 */

    spawn_tile(&nr, &nc);   /* 4. 新方块弹出 */
    pop_cell(nr, nc);

    if (g_score > g_best)
    {
        g_best = g_score;
    }
    draw_header();          /* 5. 刷新分数 */

    if (board_full() != 0U)
    {
        g_over = 1;
        draw_overlay();
    }
}

/*==============================================================================
 * 对外接口
 *============================================================================*/
void App_Init(void)
{
    LCD_Init();
    LCD_SetDir(LCD_DIR_0);

    LCD_TouchHwInit();

    g_best = 0;
    restart_game();
}

void App_Run(void)
{
    uint16_t x;
    uint16_t y;
    uint8_t  pressed;
    uint8_t  adx;
    uint8_t  ady;

    pressed = LCD_TouchScan(&x, &y);

    if (pressed != 0U)
    {
        if (g_touching == 0U)
        {
            g_touching   = 1;
            g_swipe_done = 0;
            g_start_x    = x;
            g_start_y    = y;
            g_start_tick = HAL_GetTick();
        }

        g_last_x = x;
        g_last_y = y;

        adx = dist_of(x, g_start_x);
        ady = dist_of(y, g_start_y);

        /* 滑过阈值立刻响应，不必等松手 */
        if ((g_swipe_done == 0U) && ((adx >= 30U) || (ady >= 30U)))
        {
            g_swipe_done = 1;

            if (g_over == 0U)
            {
                if (adx > ady)
                {
                    do_move((x > g_start_x) ? 1U : 0U);
                }
                else
                {
                    do_move((y > g_start_y) ? 3U : 2U);
                }
            }
        }

        /* 按住不动 3 秒：重新校准触摸 */
        if ((g_swipe_done == 0U) &&
            ((HAL_GetTick() - g_start_tick) >= 3000U) &&
            (adx < 10U) && (ady < 10U))
        {
            LCD_TouchCalibrate();

            /* 校准后参数会留在屏上，轻点一下再继续游戏 */
            while (LCD_TouchIsDown() != 0)
            {
            }
            HAL_Delay(300);
            while (LCD_TouchIsDown() == 0)
            {
            }
            while (LCD_TouchIsDown() != 0)
            {
            }

            restart_game();
            g_touching = 0;
        }

        return;
    }

    /* 松手 */
    if (g_touching == 0U)
    {
        return;
    }
    g_touching = 0;

    /* 轻点：游戏结束后用来重开 */
    if ((g_swipe_done == 0U) &&
        (dist_of(g_last_x, g_start_x) < 25U) &&
        (dist_of(g_last_y, g_start_y) < 25U))
    {
        if (g_over != 0U)
        {
            restart_game();
        }
    }

    g_swipe_done = 0;
}

#endif /* LCD_USE_TOUCH */
