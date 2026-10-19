/**
 ******************************************************************************
 * @file    app_draw.c
 * @brief   触摸画板示例
 *
 *          上电不校准，直接用 lcd.h 里的 LCD_TOUCH_DEF_* 参数。
 *          需要重新校准时，有三种方式（前两种不依赖当前坐标是否准确）：
 *            1) 上电/复位时按住屏幕不放；
 *            2) 在画布上按住不动 3 秒；
 *            3) 点右上角红色的 CAL 按钮。
 *          校准完成后屏幕会显示 4 个参数，抄回 lcd.h 的 LCD_TOUCH_DEF_*。
 ******************************************************************************
 */

#include "app.h"
#include "lcd.h"

#define UI_HEAD_H       28U
#define CAL_BTN_X       170U
#define CAL_BTN_Y       4U
#define CAL_BTN_W       66U
#define CAL_BTN_H       20U

#define PEN_COLOR       LCD_RED

/* 按住不动多久进入校准 */
#define CAL_HOLD_MS     3000U
/* 移动超过多少像素就不算“按住不动” */
#define CAL_MOVE_PX     8U

static void App_DrawUI(void)
{
    LCD_Fill(0, 0, LCD_PANEL_WIDTH, UI_HEAD_H, LCD_LIGHTGREY);

    LCD_ShowString(4, 8, "TOUCH DRAW", LCD_BLACK, LCD_LIGHTGREY, 1);

    LCD_Fill(CAL_BTN_X, CAL_BTN_Y, CAL_BTN_W, CAL_BTN_H, LCD_RED);
    LCD_ShowString((uint16_t)(CAL_BTN_X + 22U), (uint16_t)(CAL_BTN_Y + 2U),
                   "CAL", LCD_WHITE, LCD_RED, 1);

    LCD_Fill(0, UI_HEAD_H, LCD_PANEL_WIDTH, (uint16_t)(LCD_PANEL_HEIGHT - UI_HEAD_H), LCD_WHITE);
}

void App_Init(void)
{
    LCD_Init();
    LCD_SetDir(LCD_DIR_0);

#if (LCD_USE_TOUCH != 0)
    /* 只初始化触摸硬件，默认参数已经在 lcd.h 里生效 */
    LCD_TouchHwInit();

    /* 上电时如果屏幕已经被按住，直接进校准（不需要坐标，最可靠） */
    LCD_Clear(LCD_WHITE);
    LCD_ShowString(10, 150, "Hold to calibrate...", LCD_BLUE, LCD_WHITE, 1);
    HAL_Delay(200);

    if (LCD_TouchIsDown() != 0)
    {
        LCD_TouchCalibrate();
    }
#endif

    App_DrawUI();
}

void App_Run(void)
{
#if (LCD_USE_TOUCH != 0)
    static uint16_t last_x;
    static uint16_t last_y;
    static uint16_t press_x;
    static uint16_t press_y;
    static uint32_t press_tick;
    static uint8_t  drawing;
    static uint8_t  press_active;
    static uint8_t  press_moved;

    uint16_t x;
    uint16_t y;

    if (LCD_TouchScan(&x, &y) == 0)
    {
        drawing      = 0;
        press_active = 0;
        return;
    }

    if (press_active == 0)
    {
        press_active = 1;
        press_moved  = 0;
        press_tick   = HAL_GetTick();
        press_x      = x;
        press_y      = y;
    }
    else if ((x > (uint16_t)(press_x + CAL_MOVE_PX)) ||
             ((uint16_t)(x + CAL_MOVE_PX) < press_x) ||
             (y > (uint16_t)(press_y + CAL_MOVE_PX)) ||
             ((uint16_t)(y + CAL_MOVE_PX) < press_y))
    {
        press_moved = 1;
    }

    /* 按住不动 3 秒：重新校准 */
    if ((press_moved == 0) && ((HAL_GetTick() - press_tick) >= CAL_HOLD_MS))
    {
        while (LCD_TouchIsDown() != 0)
        {
        }

        LCD_TouchCalibrate();
        App_DrawUI();

        press_active = 0;
        drawing      = 0;
        return;
    }

    /* 点中顶部右侧的 CAL 按钮：重新校准 */
    if ((y < UI_HEAD_H) && (x >= CAL_BTN_X))
    {
        while (LCD_TouchIsDown() != 0)
        {
        }

        LCD_TouchCalibrate();
        App_DrawUI();

        press_active = 0;
        drawing      = 0;
        return;
    }

    /* 顶部信息栏不画 */
    if (y < UI_HEAD_H)
    {
        drawing = 0;
        return;
    }

    /* 连续划动时用线段连接上一点，单点则画一个 3x3 的粗点 */
    if (drawing != 0)
    {
        LCD_DrawLine(last_x, last_y, x, y, PEN_COLOR);
    }
    else
    {
        uint16_t left = (x > 1U) ? (uint16_t)(x - 1U) : 0U;
        uint16_t top  = (y > 1U) ? (uint16_t)(y - 1U) : 0U;

        LCD_Fill(left, top, 3, 3, PEN_COLOR);
    }

    last_x  = x;
    last_y  = y;
    drawing = 1;
#endif
}
