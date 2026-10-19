/**
 ******************************************************************************
 * @file    App.c
 * @brief   2048 小游戏（240x320 竖屏，触摸滑动操作）
 *
 *          操作：在屏幕上滑动 —— 上/下/左/右，相同数字自动合并。
 *          目标：拼出 2048。
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
#define HEADER_H        52

/* ---------------- 配色 ---------------- */
#define C_BOARD         0xBD74
#define C_EMPTY         0xCE16
#define C_TEXT_DARK     0x0000
#define C_TEXT_LIGHT    0xFFFF

/* ---------------- 游戏状态 ---------------- */
static uint16_t g_board[GRID_SIZE][GRID_SIZE];
static uint32_t g_score;
static uint32_t g_best;
static uint8_t  g_over;
static uint32_t g_rng;

/* 触摸状态 */
static uint8_t  g_touching;
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

static uint16_t cell_x(uint8_t col)
{
    return (uint16_t)(BOARD_X + col * (CELL_SIZE + CELL_GAP));
}

static uint16_t cell_y(uint8_t row)
{
    return (uint16_t)(BOARD_Y + row * (CELL_SIZE + CELL_GAP));
}

/*==============================================================================
 * 绘制
 *============================================================================*/
static void draw_header(void)
{
    LCD_Fill(0, 0, LCD_PANEL_WIDTH, HEADER_H, C_BOARD);

    LCD_ShowString(8, 6, "SCORE", C_TEXT_LIGHT, C_BOARD, 1);
    LCD_ShowNum(8, 22, g_score, 1, C_TEXT_LIGHT, C_BOARD, 2);

    LCD_ShowString(140, 6, "BEST", C_TEXT_LIGHT, C_BOARD, 1);
    LCD_ShowNum(140, 22, g_best, 1, C_TEXT_LIGHT, C_BOARD, 2);

    LCD_ShowString(84, 6, "2048", 0xFFE0, C_BOARD, 1);
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

    LCD_Fill(0, BOARD_Y - CELL_GAP, LCD_PANEL_WIDTH,
             (uint16_t)(GRID_SIZE * (CELL_SIZE + CELL_GAP)), C_BOARD);

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
 * 游戏逻辑
 *============================================================================*/
static void spawn_tile(void)
{
    uint8_t empty[GRID_SIZE * GRID_SIZE];
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
}

/* 一行向左压缩合并，返回是否发生变化 */
static uint8_t slide_line(uint16_t *line, uint32_t *gain)
{
    uint16_t out[GRID_SIZE] = {0, 0, 0, 0};
    uint8_t  w = 0;
    uint8_t  merged_last = 0;
    uint8_t  changed = 0;
    uint8_t  i;

    for (i = 0; i < GRID_SIZE; i++)
    {
        uint16_t v = line[i];

        if (v == 0U)
        {
            continue;
        }

        if ((w > 0U) && (out[w - 1U] == v) && (merged_last == 0U))
        {
            out[w - 1U] = (uint16_t)(v * 2U);
            *gain += out[w - 1U];
            merged_last = 1;
        }
        else
        {
            out[w++] = v;
            merged_last = 0;
        }
    }

    for (i = 0; i < GRID_SIZE; i++)
    {
        if (line[i] != out[i])
        {
            changed = 1;
        }
        line[i] = out[i];
    }

    return changed;
}

/* dir: 0=左 1=右 2=上 3=下 */
static uint8_t move_board(uint8_t dir)
{
    uint16_t line[GRID_SIZE];
    uint32_t gain = 0;
    uint8_t  changed = 0;
    uint8_t  i;
    uint8_t  j;
    uint8_t  k;

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
        }

        if (slide_line(line, &gain) != 0U)
        {
            changed = 1;
        }

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

    spawn_tile();
    spawn_tile();

    draw_header();
    draw_board();
    draw_footer();
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

    pressed = LCD_TouchScan(&x, &y);

    if (pressed != 0U)
    {
        if (g_touching == 0U)
        {
            g_touching   = 1;
            g_start_x    = x;
            g_start_y    = y;
            g_start_tick = HAL_GetTick();
        }

        g_last_x = x;
        g_last_y = y;

        /* 按住不动 3 秒：重新校准触摸 */
        if (((HAL_GetTick() - g_start_tick) >= 3000U) &&
            (((x > g_start_x) ? (x - g_start_x) : (g_start_x - x)) < 10U) &&
            (((y > g_start_y) ? (y - g_start_y) : (g_start_y - y)) < 10U))
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

    /* 以下为松手时的处理 */
    if (g_touching == 0U)
    {
        return;
    }
    g_touching = 0;

    {
        int16_t dx  = (int16_t)g_last_x - (int16_t)g_start_x;
        int16_t dy  = (int16_t)g_last_y - (int16_t)g_start_y;
        int16_t adx = (dx < 0) ? (int16_t)(-dx) : dx;
        int16_t ady = (dy < 0) ? (int16_t)(-dy) : dy;

        /* 轻点 */
        if ((adx < 25) && (ady < 25))
        {
            if (g_over != 0U)
            {
                restart_game();
            }
            return;
        }

        if (g_over != 0U)
        {
            return;
        }

        {
            uint8_t moved;

            if (adx > ady)
            {
                moved = move_board((dx > 0) ? 1U : 0U);
            }
            else
            {
                moved = move_board((dy > 0) ? 3U : 2U);
            }

            if (moved == 0U)
            {
                return;
            }

            spawn_tile();

            if (g_score > g_best)
            {
                g_best = g_score;
            }

            draw_header();
            draw_board();

            if (board_full() != 0U)
            {
                g_over = 1;
                draw_overlay();
            }
        }
    }
}

#endif /* LCD_USE_TOUCH */
