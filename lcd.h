/**
 ******************************************************************************
 * @file    lcd.h
 * @brief   Minimal driver for ALIENTEK ATK-MD0280 2.8" TFT (240x320)
 *          Panel controllers: ILI9341 (ID 0x9341) / ST7789V (ID 0x8552)
 *          Bus: FSMC Bank1 NE4, 16-bit, RS = A10 (PG0)
 *          Touch (optional): XPT2046 via bit-banged SPI
 *
 * Usage:
 *     1. Add lcd.c to your project and add this folder to include paths.
 *     2. #include "lcd.h"
 *     3. LCD_Init();  then use LCD_Clear / LCD_ShowString / ...
 *
 * The driver configures the FSMC and all GPIO by itself, so nothing has to be
 * added in CubeMX. Only HAL_GPIO and HAL_Delay are used (both already present
 * in a CubeMX project).
 *
 * Pin map (ALIENTEK STM32F103ZE board, LCD connector):
 *     CS  = PG12 (FSMC_NE4)      RS  = PG0  (FSMC_A10)
 *     RD  = PD4                  WR  = PD5
 *     D0..D3   = PD14, PD15, PD0, PD1
 *     D4..D12  = PE7..PE15
 *     D13..D15 = PD8, PD9, PD10
 *     BL  = PB0 (high = backlight on)
 *     Touch: PEN=PF10, TCS=PF11, CLK=PB1, MOSI=PF9, MISO=PB2
 ******************************************************************************
 */

#ifndef __LCD_H
#define __LCD_H

#include "stm32f1xx_hal.h"

#ifdef __cplusplus
extern "C" {
#endif

/*==============================================================================
 * 1. 面板与总线配置（换板子/换芯片主要改这一段）
 *============================================================================*/

/* 面板分辨率（0° 方向） */
#define LCD_PANEL_WIDTH             240
#define LCD_PANEL_HEIGHT            320

/* 用哪一路 FSMC 片选：1..4，对应 NE1..NE4
 * NE1 = 0x60000000, NE2 = 0x64000000, NE3 = 0x68000000, NE4 = 0x6C000000 */
#define LCD_FSMC_NE                 4U

/* RS(命令/数据选择) 接在 FSMC 的哪根地址线上，A10 就是 10
 * 注意：改了这个，LCD_FSMC_PIN_GROUPS 里对应的地址线引脚也要改 */
#define LCD_FSMC_RS_BIT             10U

/* 片选基地址，由 NE 自动算出，不用改 */
#define LCD_FSMC_BASE               (0x60000000UL + (((uint32_t)LCD_FSMC_NE - 1UL) * 0x04000000UL))

/* FSMC 时序，单位 = HCLK（72 MHz 时 1 HCLK = 13.9 ns）
 * 读：ID/GRAM 读很慢，数据相位给足
 * 写：这代面板 tWC >= 66 ns，(DATAST+1) 个 HCLK + 1 个地址建立周期，
 *     72 MHz 下 DATAST=3 约 70 ns。屏幕不稳定时优先加大 LCD_WRITE_DATAST。 */
#define LCD_READ_ADDSET             0U
#define LCD_READ_DATAST             15U
#define LCD_WRITE_ADDSET            0U
#define LCD_WRITE_DATAST            3U

/* 背光引脚 */
#define LCD_BL_PORT                 GPIOB
#define LCD_BL_PIN                  GPIO_PIN_0

/* 需要打开时钟的 GPIO 端口（按实际用到的端口增删） */
#define LCD_GPIO_CLK_ENABLE()       do {          \
                                        __HAL_RCC_GPIOD_CLK_ENABLE(); \
                                        __HAL_RCC_GPIOE_CLK_ENABLE(); \
                                        __HAL_RCC_GPIOG_CLK_ENABLE(); \
                                        __HAL_RCC_GPIOB_CLK_ENABLE(); \
                                    } while (0)

/* FSMC 用到的全部引脚：{端口, 引脚掩码}
 * 正点原子 F103ZE 接法：D0=PD14 D1=PD15 D2=PD0 D3=PD1 D4~D12=PE7~PE15
 *                       D13=PD8 D14=PD9 D15=PD10
 *                       CS=PG12(NE4) RS=PG0(A10) RD=PD4 WR=PD5
 * 换板子时把下面几行改成你的接线即可，驱动其余部分不用动。 */
#define LCD_FSMC_PIN_GROUPS                                                   \
    { GPIOD, GPIO_PIN_0  | GPIO_PIN_1  | GPIO_PIN_4  | GPIO_PIN_5  |         \
             GPIO_PIN_8  | GPIO_PIN_9  | GPIO_PIN_10 | GPIO_PIN_14 |         \
             GPIO_PIN_15 },                                                   \
    { GPIOE, GPIO_PIN_7  | GPIO_PIN_8  | GPIO_PIN_9  | GPIO_PIN_10 |         \
             GPIO_PIN_11 | GPIO_PIN_12 | GPIO_PIN_13 | GPIO_PIN_14 |         \
             GPIO_PIN_15 },                                                   \
    { GPIOG, GPIO_PIN_0  | GPIO_PIN_12 }

/* 触摸（XPT2046）引脚，不用触摸可以不看 */
#define LCD_TOUCH_PEN_PORT          GPIOF
#define LCD_TOUCH_PEN_PIN           GPIO_PIN_10
#define LCD_TOUCH_TCS_PORT          GPIOF
#define LCD_TOUCH_TCS_PIN           GPIO_PIN_11
#define LCD_TOUCH_CLK_PORT          GPIOB
#define LCD_TOUCH_CLK_PIN           GPIO_PIN_1
#define LCD_TOUCH_MOSI_PORT         GPIOF
#define LCD_TOUCH_MOSI_PIN          GPIO_PIN_9
#define LCD_TOUCH_MISO_PORT         GPIOB
#define LCD_TOUCH_MISO_PIN          GPIO_PIN_2
#define LCD_TOUCH_GPIO_CLK_ENABLE() do {          \
                                        __HAL_RCC_GPIOB_CLK_ENABLE(); \
                                        __HAL_RCC_GPIOF_CLK_ENABLE(); \
                                    } while (0)

/* Set to 1 to compile the resistive touch driver (XPT2046).
 * With touch enabled the panel starts with the LCD_TOUCH_DEF_* parameters
 * below, no boot-time calibration is needed.
 * Can also be overridden from the compiler command line: -DLCD_USE_TOUCH=1 */
#ifndef LCD_USE_TOUCH
#define LCD_USE_TOUCH               1
#endif

/*==============================================================================
 * Colors (RGB565)
 *============================================================================*/
#define LCD_BLACK                   0x0000
#define LCD_NAVY                    0x000F
#define LCD_DARKGREEN               0x03E0
#define LCD_DARKCYAN                0x03EF
#define LCD_MAROON                  0x7800
#define LCD_PURPLE                  0x780F
#define LCD_OLIVE                   0x7BE0
#define LCD_LIGHTGREY               0xC618
#define LCD_DARKGREY                0x7BEF
#define LCD_BLUE                    0x001F
#define LCD_GREEN                   0x07E0
#define LCD_CYAN                    0x07FF
#define LCD_RED                     0xF800
#define LCD_MAGENTA                 0xF81F
#define LCD_YELLOW                  0xFFE0
#define LCD_WHITE                   0xFFFF
#define LCD_ORANGE                  0xFD20
#define LCD_GREENYELLOW             0xAFE5
#define LCD_PINK                    0xF81F

/* Build a RGB565 color from 8-bit components */
#define LCD_RGB565(r, g, b)         ((uint16_t)((((r) & 0xF8) << 8) | (((g) & 0xFC) << 3) | ((b) >> 3)))

/*==============================================================================
 * Display directions (rotation)
 *============================================================================*/
#define LCD_DIR_0                   0       /* 240 x 320, portrait            */
#define LCD_DIR_90                  1       /* 320 x 240, landscape           */
#define LCD_DIR_180                 2       /* 240 x 320, upside down         */
#define LCD_DIR_270                 3       /* 320 x 240, landscape flipped   */

/*==============================================================================
 * API
 *============================================================================*/

/* Init: configures GPIO + FSMC, detects the panel and initializes it.
 * Returns the controller id (0x9341 or 0x8552), or 0 if no known panel found. */
uint16_t LCD_Init(void);

/* Same as LCD_Init() but skips detection and uses "id" (0x9341 or 0x8552). */
void     LCD_InitAs(uint16_t id);

void     LCD_SetDir(uint8_t dir);              /* LCD_DIR_0 .. LCD_DIR_270 */
uint8_t  LCD_GetDir(void);
uint16_t LCD_GetWidth(void);                   /* current width  (depends on dir) */
uint16_t LCD_GetHeight(void);                  /* current height (depends on dir) */
uint16_t LCD_ReadID(void);

void     LCD_BackLight(uint8_t on);            /* 1 = on, 0 = off */
void     LCD_DisplayOn(void);
void     LCD_DisplayOff(void);

/* Drawing, all coordinates are inclusive top-left based */
void     LCD_Clear(uint16_t color);
void     LCD_Fill(uint16_t x, uint16_t y, uint16_t w, uint16_t h, uint16_t color);
void     LCD_DrawPoint(uint16_t x, uint16_t y, uint16_t color);
uint16_t LCD_ReadPoint(uint16_t x, uint16_t y);
void     LCD_DrawLine(uint16_t x1, uint16_t y1, uint16_t x2, uint16_t y2, uint16_t color);
void     LCD_DrawRect(uint16_t x, uint16_t y, uint16_t w, uint16_t h, uint16_t color);
void     LCD_DrawCircle(uint16_t x, uint16_t y, uint16_t r, uint16_t color);
void     LCD_ShowImage(uint16_t x, uint16_t y, uint16_t w, uint16_t h, const uint16_t *img);

/* Text: built-in 8x16 ASCII font, "size" is the scale factor (1, 2, 3 ...).
 * Each character occupies (8 * size) x (16 * size) pixels.
 * The character cell is painted with "bg", so text erases what is under it.
 * Newline ('\n') goes back to the start column and moves down one line. */
void     LCD_ShowChar(uint16_t x, uint16_t y, char ch,
                      uint16_t fg, uint16_t bg, uint8_t size);
void     LCD_ShowString(uint16_t x, uint16_t y, const char *str,
                        uint16_t fg, uint16_t bg, uint8_t size);
void     LCD_ShowNum(uint16_t x, uint16_t y, uint32_t num, uint8_t len,
                     uint16_t fg, uint16_t bg, uint8_t size);

/* Draw a few color bars and text, handy to check the wiring. */
void     LCD_Test(void);

#if (LCD_USE_TOUCH != 0)
/*------------------------------------------------------------------------------
 * 开机默认校准参数
 *
 * 上电后直接用这组值，不需要每次校准。
 * 觉得触点和手指对不上时，调用 LCD_TouchCalibrate()（或点屏幕上的 CAL），
 * 校准完成后屏幕会显示新的 4 个值，把它们替换到下面即可。
 *---------------------------------------------------------------------------*/
#define LCD_TOUCH_DEF_X_SCALE       0.0664f     /* 每 1 个 ADC 码对应多少像素 */
#define LCD_TOUCH_DEF_Y_SCALE       0.0905f
#define LCD_TOUCH_DEF_X_CENTER      2122U       /* 屏幕中心点的 ADC 值 */
#define LCD_TOUCH_DEF_Y_CENTER      2019U

typedef struct
{
    float    x_scale;
    float    y_scale;
    uint16_t x_center;
    uint16_t y_center;
} lcd_touch_cal_t;

/* Initialize the touch hardware only (no calibration, does not block).
 * The default parameters above are already active at this point. */
void     LCD_TouchHwInit(void);

/* 5 点校准：画十字、等点击、算参数、把新参数显示 8 秒，最后返回结果。
 * 阻塞函数，直接调用即可；校准失败时保留原参数并返回原参数。 */
lcd_touch_cal_t LCD_TouchCalibrate(void);

/* 把当前校准参数显示在屏上（校准结束会自动调用，也可随时手动调） */
void     LCD_TouchShowCal(void);

/* Touch hardware init + calibration, kept for convenience. */
void     LCD_TouchInit(void);

/* Read / write the active calibration (e.g. load from flash, or print on UART). */
void     LCD_TouchGetCal(lcd_touch_cal_t *cal);
void     LCD_TouchSetCal(const lcd_touch_cal_t *cal);

/* Returns 1 and fills x/y when the screen is pressed, 0 otherwise. */
uint8_t  LCD_TouchScan(uint16_t *x, uint16_t *y);

/* 1 while the panel is pressed. Reads the PEN line only, so it works even
 * when the calibration is wrong (handy to enter calibration). */
uint8_t  LCD_TouchIsDown(void);
#endif

#ifdef __cplusplus
}
#endif

#endif /* __LCD_H */
