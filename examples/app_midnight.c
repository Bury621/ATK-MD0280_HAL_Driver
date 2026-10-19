/**
 ******************************************************************************
 * @file    App.c
 * @brief   MIDNIGHT —— 恐怖小游戏（240x320 触摸）
 *
 *          全黑房间里只有你手里的一束微光。拖动屏幕移动光源，
 *          在黑暗中找出 5 把钥匙，然后找到出口。
 *          有什么东西一直在朝光源爬过来，只有被照到时才看得见。
 *
 *          操作：按住拖动 = 移动光源；结束后轻点重开。
 *          需要 lcd.h 里 LCD_USE_TOUCH = 1。
 ******************************************************************************
 */

#include "app.h"
#include "lcd.h"

#if (LCD_USE_TOUCH == 0)

void App_Init(void)
{
    LCD_Init();
    LCD_SetDir(LCD_DIR_0);
    LCD_Clear(LCD_WHITE);
    LCD_ShowString(10, 10, "Enable LCD_USE_TOUCH", LCD_RED, LCD_WHITE, 1);
    LCD_ShowString(10, 30, "in lcd.h to play", LCD_BLUE, LCD_WHITE, 1);
}

void App_Run(void)
{
}

#else

/* ---------------- 参数 ---------------- */
#define PLAY_H          292U        /* 上方游戏区高度，下面是状态栏 */
#define HUD_Y           294U
#define LIGHT_R         46U         /* 光照半径 */
#define KEY_COUNT       5U
#define PICK_R          16U         /* 拾取 / 被抓住的距离 */
#define DOOR_R          18U
#define FRAME_MS        40U         /* 约 25 帧 */

/* ---------------- 配色 ---------------- */
#define C_BLACK         0x0000
#define C_DIM           0x2104      /* 很暗的灰，画准星用 */
#define C_KEY           0xFDA0      /* 钥匙：暗金 */
#define C_GHOST         0xDEFB      /* 鬼影：惨白 */
#define C_EYE           0xF800
#define C_DOOR          0x07E0
#define C_WARN          0x3000
#define C_RED           0xF800
#define C_WHITE         0xFFFF

/* ---------------- 状态 ---------------- */
typedef enum
{
    ST_PLAY = 0,
    ST_CAUGHT,
    ST_ESCAPED
} game_state_t;

static game_state_t g_state;
static uint8_t  g_need_release;
static uint16_t g_light_x;
static uint16_t g_light_y;
static uint16_t g_key_x[KEY_COUNT];
static uint16_t g_key_y[KEY_COUNT];
static uint8_t  g_key_taken[KEY_COUNT];
static uint8_t  g_keys_left;
static uint16_t g_ghost_x;
static uint16_t g_ghost_y;
static uint16_t g_door_x;
static uint16_t g_door_y;
static uint8_t  g_door_on;
static uint32_t g_start_ms;
static uint32_t g_last_ms;
static uint32_t g_rng;
static uint8_t  g_hud_keys;
static uint32_t g_hud_sec;

/*==============================================================================
 * 工具
 *============================================================================*/
static uint32_t rng_next(void)
{
    g_rng = g_rng * 1664525UL + 1013904223UL;
    return (g_rng >> 16);
}

static uint32_t dist2(int32_t ax, int32_t ay, int32_t bx, int32_t by)
{
    int32_t dx = ax - bx;
    int32_t dy = ay - by;

    return (uint32_t)((dx * dx) + (dy * dy));
}

/* 在游戏区里随机取点，尽量离光源远一些 */
static void pick_spot(uint16_t *x, uint16_t *y, uint16_t margin)
{
    uint8_t i;

    for (i = 0; i < 40U; i++)
    {
        *x = (uint16_t)(margin + (rng_next() % (LCD_PANEL_WIDTH - 2U * margin)));
        *y = (uint16_t)(margin + (rng_next() % (PLAY_H - 2U * margin)));

        if (dist2(*x, *y, g_light_x, g_light_y) > (90U * 90U))
        {
            return;
        }
    }
}

/*==============================================================================
 * 绘制
 *============================================================================*/
static void draw_key(uint16_t x, uint16_t y)
{
    LCD_Fill((uint16_t)(x - 2U), (uint16_t)(y - 6U), 5, 13, C_KEY);
    LCD_Fill((uint16_t)(x - 6U), (uint16_t)(y - 9U), 13, 6, C_KEY);
}

static void draw_ghost(void)
{
    uint8_t i;

    LCD_DrawCircle(g_ghost_x, g_ghost_y, 14, C_GHOST);

    for (i = 0; i < 6U; i++)
    {
        LCD_DrawPoint((uint16_t)(g_ghost_x - 9U), (uint16_t)(g_ghost_y + 12U + i), C_GHOST);
        LCD_DrawPoint((uint16_t)(g_ghost_x + 9U), (uint16_t)(g_ghost_y + 12U + i), C_GHOST);
    }

    LCD_Fill((uint16_t)(g_ghost_x - 8U), (uint16_t)(g_ghost_y - 5U), 5, 5, C_EYE);
    LCD_Fill((uint16_t)(g_ghost_x + 4U), (uint16_t)(g_ghost_y - 5U), 5, 5, C_EYE);
}

static void draw_door(void)
{
    LCD_DrawRect((uint16_t)(g_door_x - 14U), (uint16_t)(g_door_y - 18U), 29, 37, C_DOOR);
    LCD_DrawRect((uint16_t)(g_door_x - 11U), (uint16_t)(g_door_y - 15U), 23, 31, C_DOOR);
    LCD_Fill((uint16_t)(g_door_x + 5U), g_door_y, 3, 3, C_DOOR);
}

static void draw_light_cursor(void)
{
    LCD_DrawLine((uint16_t)(g_light_x - 6U), g_light_y, (uint16_t)(g_light_x + 6U), g_light_y, C_DIM);
    LCD_DrawLine(g_light_x, (uint16_t)(g_light_y - 6U), g_light_x, (uint16_t)(g_light_y + 6U), C_DIM);
}

static void draw_hud(void)
{
    uint8_t i;

    LCD_Fill(0, (uint16_t)(HUD_Y - 2U), LCD_PANEL_WIDTH,
             (uint16_t)(LCD_PANEL_HEIGHT - HUD_Y + 2U), C_BLACK);

    for (i = 0; i < KEY_COUNT; i++)
    {
        LCD_Fill((uint16_t)(6U + i * 14U), (uint16_t)(HUD_Y + 6U), 9, 9,
                 (g_key_taken[i] != 0U) ? C_KEY : C_WARN);
    }

    LCD_ShowString(86, (uint16_t)(HUD_Y + 5U), "T", C_DIM, C_BLACK, 1);
    LCD_ShowNum(98, (uint16_t)(HUD_Y + 5U), g_hud_sec, 3, C_WHITE, C_BLACK, 1);
}

/* 一帧画面：全黑，只有光照范围内的东西才画出来 */
static void draw_frame(void)
{
    uint32_t r2 = (uint32_t)LIGHT_R * (uint32_t)LIGHT_R;
    uint32_t gd;
    uint8_t  i;

    LCD_Fill(0, 0, LCD_PANEL_WIDTH, PLAY_H, C_BLACK);

    for (i = 0; i < KEY_COUNT; i++)
    {
        if ((g_key_taken[i] == 0U) &&
            (dist2(g_key_x[i], g_key_y[i], g_light_x, g_light_y) < r2))
        {
            draw_key(g_key_x[i], g_key_y[i]);
        }
    }

    if ((g_door_on != 0U) &&
        (dist2(g_door_x, g_door_y, g_light_x, g_light_y) < r2))
    {
        draw_door();
    }

    gd = dist2(g_ghost_x, g_ghost_y, g_light_x, g_light_y);

    if (gd < (r2 * 2U))
    {
        draw_ghost();
    }

    /* 离得太近：屏幕两侧泛红，提示它就在附近 */
    if (gd < (80U * 80U))
    {
        LCD_Fill(0, 0, 3, PLAY_H, C_RED);
        LCD_Fill((uint16_t)(LCD_PANEL_WIDTH - 3U), 0, 3, PLAY_H, C_RED);
    }
    else
    {
        draw_light_cursor();
    }
}

/*==============================================================================
 * 结算画面
 *============================================================================*/
static void jump_scare(void)
{
    LCD_Fill(0, 0, LCD_PANEL_WIDTH, LCD_PANEL_HEIGHT, C_WHITE);

    LCD_Fill(46, 96, 46, 34, C_BLACK);      /* 左眼 */
    LCD_Fill(148, 96, 46, 34, C_BLACK);     /* 右眼 */
    LCD_Fill(96, 176, 48, 70, C_BLACK);     /* 嘴 */
    LCD_Fill(86, 246, 68, 8, C_BLACK);

    HAL_Delay(700);

    LCD_Fill(0, 0, LCD_PANEL_WIDTH, LCD_PANEL_HEIGHT, C_RED);
    HAL_Delay(120);
    LCD_Fill(0, 0, LCD_PANEL_WIDTH, LCD_PANEL_HEIGHT, C_BLACK);
}

static void show_caught(void)
{
    LCD_Clear(C_BLACK);
    LCD_ShowString(40, 90, "IT FOUND YOU", C_RED, C_BLACK, 2);
    LCD_ShowString(70, 140, "GAME OVER", C_WHITE, C_BLACK, 1);
    LCD_ShowString(58, 260, "TAP TO RETRY", C_DIM, C_BLACK, 1);
}

static void show_escaped(uint32_t sec)
{
    LCD_Clear(C_BLACK);
    LCD_ShowString(28, 86, "YOU ESCAPED", C_DOOR, C_BLACK, 2);
    LCD_ShowString(72, 150, "time", C_DIM, C_BLACK, 1);
    LCD_ShowNum(116, 150, sec, 3, C_WHITE, C_BLACK, 1);
    LCD_ShowString(60, 170, "seconds", C_DIM, C_BLACK, 1);
    LCD_ShowString(46, 260, "TAP TO PLAY AGAIN", C_DIM, C_BLACK, 1);
}

/*==============================================================================
 * 游戏流程
 *============================================================================*/
static void new_game(void)
{
    uint8_t i;

    g_rng       = HAL_GetTick() | 1U;
    g_state     = ST_PLAY;
    g_light_x   = LCD_PANEL_WIDTH / 2U;
    g_light_y   = PLAY_H / 2U;
    g_keys_left = KEY_COUNT;
    g_door_on   = 0;
    g_hud_keys  = 0xFFU;
    g_hud_sec   = 0xFFFFFFFFUL;
    g_need_release = 0;

    for (i = 0; i < KEY_COUNT; i++)
    {
        g_key_taken[i] = 0;
        pick_spot(&g_key_x[i], &g_key_y[i], 18);
    }

    do
    {
        pick_spot(&g_ghost_x, &g_ghost_y, 16);
    } while (dist2(g_ghost_x, g_ghost_y, g_light_x, g_light_y) < (150U * 150U));

    g_start_ms = HAL_GetTick();
    g_last_ms  = g_start_ms;

    LCD_Fill(0, 0, LCD_PANEL_WIDTH, LCD_PANEL_HEIGHT, C_BLACK);
    draw_hud();
    draw_frame();
}

/* 整数开方，够用就行 */
static uint32_t isqrt32(uint32_t v)
{
    uint32_t r = 0;
    uint32_t bit = 1UL << 30;

    while (bit > v)
    {
        bit >>= 2;
    }

    while (bit != 0U)
    {
        if (v >= (r + bit))
        {
            v -= (r + bit);
            r = (r >> 1) + bit;
        }
        else
        {
            r >>= 1;
        }

        bit >>= 2;
    }

    return r;
}

static void update_ghost(uint32_t dt_ms)
{
    int32_t  dx = (int32_t)g_light_x - (int32_t)g_ghost_x;
    int32_t  dy = (int32_t)g_light_y - (int32_t)g_ghost_y;
    uint32_t d2 = (uint32_t)(dx * dx) + (uint32_t)(dy * dy);
    uint32_t d;
    uint32_t speed;
    int32_t  step;

    if (d2 == 0U)
    {
        return;
    }

    d = isqrt32(d2);

    if (d < 1U)
    {
        d = 1U;
    }

    /* 捡到的钥匙越多，它越快 */
    speed = 18U + (uint32_t)(KEY_COUNT - g_keys_left) * 7U;      /* px/s */
    step  = (int32_t)((speed * dt_ms) / 1000U);

    if (step < 1)
    {
        step = 1;
    }

    g_ghost_x = (uint16_t)((int32_t)g_ghost_x + ((dx * step) / (int32_t)d));
    g_ghost_y = (uint16_t)((int32_t)g_ghost_y + ((dy * step) / (int32_t)d));

    if (g_ghost_x < 8U)  { g_ghost_x = 8U; }
    if (g_ghost_y < 8U)  { g_ghost_y = 8U; }
    if (g_ghost_x > (LCD_PANEL_WIDTH - 8U)) { g_ghost_x = LCD_PANEL_WIDTH - 8U; }
    if (g_ghost_y > (PLAY_H - 8U))          { g_ghost_y = PLAY_H - 8U; }
}

static void update_play(uint32_t now)
{
    uint32_t dt = now - g_last_ms;
    uint32_t sec;
    uint8_t  i;

    if (dt < FRAME_MS)
    {
        return;
    }

    g_last_ms = now;

    if (dt > 200U)
    {
        dt = 200U;
    }

    if (dist2(g_ghost_x, g_ghost_y, g_light_x, g_light_y) < (PICK_R * PICK_R))
    {
        g_state = ST_CAUGHT;
        g_need_release = 1;
        jump_scare();
        show_caught();
        return;
    }

    update_ghost(dt);

    for (i = 0; i < KEY_COUNT; i++)
    {
        if ((g_key_taken[i] == 0U) &&
            (dist2(g_key_x[i], g_key_y[i], g_light_x, g_light_y) < (PICK_R * PICK_R)))
        {
            g_key_taken[i] = 1;
            g_keys_left--;

            if (g_keys_left == 0U)
            {
                g_door_on = 1;
                pick_spot(&g_door_x, &g_door_y, 20);
            }
        }
    }

    if ((g_door_on != 0U) &&
        (dist2(g_door_x, g_door_y, g_light_x, g_light_y) < (DOOR_R * DOOR_R)))
    {
        g_state = ST_ESCAPED;
        g_need_release = 1;
        show_escaped((now - g_start_ms) / 1000U);
        return;
    }

    sec = (now - g_start_ms) / 1000U;

    if ((g_keys_left != g_hud_keys) || (sec != g_hud_sec))
    {
        g_hud_keys = g_keys_left;
        g_hud_sec  = sec;
        draw_hud();
    }

    draw_frame();
}

/*==============================================================================
 * 对外接口
 *============================================================================*/
void App_Init(void)
{
    LCD_Init();
    LCD_SetDir(LCD_DIR_0);

    LCD_TouchHwInit();

    new_game();
}

void App_Run(void)
{
    uint16_t x;
    uint16_t y;
    uint8_t  pressed = LCD_TouchScan(&x, &y);

    if (g_state == ST_PLAY)
    {
        if (pressed != 0U)
        {
            if (x >= LCD_PANEL_WIDTH) { x = LCD_PANEL_WIDTH - 1U; }
            if (y >= PLAY_H)          { y = PLAY_H - 1U; }

            g_light_x = x;
            g_light_y = y;
        }

        update_play(HAL_GetTick());
        return;
    }

    /* 结算画面：先等松手，再轻点重开 */
    if (g_need_release != 0U)
    {
        if (pressed == 0U)
        {
            g_need_release = 0;
        }
        return;
    }

    if (pressed != 0U)
    {
        new_game();
    }
}

#endif /* LCD_USE_TOUCH */
