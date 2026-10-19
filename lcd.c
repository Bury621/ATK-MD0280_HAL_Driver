/**
 ******************************************************************************
 * @file    lcd.c
 * @brief   Minimal driver for ALIENTEK ATK-MD0280 2.8" TFT (240x320)
 *          ILI9341 / ST7789V, FSMC 16-bit bus, optional XPT2046 touch.
 *
 *          No CubeMX changes are needed: the FSMC and every GPIO used by the
 *          panel are configured here. Only HAL_GPIO + HAL_Delay are used.
 ******************************************************************************
 */

#include "lcd.h"
#include "lcd_font.h"

#if !defined(FSMC_Bank1) || !defined(FSMC_Bank1E)
#error "This driver needs an STM32F103 with FSMC (xE/xG density, e.g. STM32F103ZE)."
#endif

/*==============================================================================
 * Local definitions
 *============================================================================*/
#define LCD_CMD_ADDR    (LCD_FSMC_BASE | (((1UL << LCD_FSMC_RS_BIT) - 1UL) << 1))
#define LCD_DAT_ADDR    (LCD_FSMC_BASE | ((1UL << LCD_FSMC_RS_BIT) << 1))

#define LCD_CMD         (*(volatile uint16_t *)LCD_CMD_ADDR)
#define LCD_DAT         (*(volatile uint16_t *)LCD_DAT_ADDR)

#define LCD_CHIP_ILI9341    0x9341U
#define LCD_CHIP_ST7789V    0x8552U

/*==============================================================================
 * Static state
 *============================================================================*/
static uint16_t g_lcd_width  = LCD_PANEL_WIDTH;
static uint16_t g_lcd_height = LCD_PANEL_HEIGHT;
static uint8_t  g_lcd_dir    = LCD_DIR_0;
static uint16_t g_lcd_id     = 0;

/*==============================================================================
 * Low level bus access
 *============================================================================*/
static inline void lcd_write_cmd(uint16_t cmd)
{
    LCD_CMD = cmd;
}

static inline void lcd_write_data(uint16_t dat)
{
    LCD_DAT = dat;
}

static inline uint16_t lcd_read_data(void)
{
    uint16_t dat;

    __NOP();
    __NOP();
    dat = LCD_DAT;

    return dat;
}

/* Set the drawing window and start a memory write (0x2C).
 * 0x2A / 0x2B take FOUR 8-bit parameters each: start high, start low,
 * end high, end low. The panel is 16-bit per pixel but its command
 * parameters are bytes, so a 16-bit write would send twice as many. */
static void lcd_set_window(uint16_t x, uint16_t y, uint16_t w, uint16_t h)
{
    uint16_t xe = (uint16_t)(x + w - 1U);
    uint16_t ye = (uint16_t)(y + h - 1U);

    lcd_write_cmd(0x2A);                    /* column address */
    lcd_write_data((uint16_t)(x >> 8));
    lcd_write_data((uint16_t)(x & 0xFFU));
    lcd_write_data((uint16_t)(xe >> 8));
    lcd_write_data((uint16_t)(xe & 0xFFU));

    lcd_write_cmd(0x2B);                    /* page address */
    lcd_write_data((uint16_t)(y >> 8));
    lcd_write_data((uint16_t)(y & 0xFFU));
    lcd_write_data((uint16_t)(ye >> 8));
    lcd_write_data((uint16_t)(ye & 0xFFU));

    lcd_write_cmd(0x2C);                    /* memory write */
}

/* Write one register plus a list of parameters. */
static void lcd_write_reg(uint16_t cmd, const uint8_t *dat, uint16_t len)
{
    uint16_t i;

    lcd_write_cmd(cmd);
    for (i = 0; i < len; i++)
    {
        lcd_write_data(dat[i]);
    }
}

/*==============================================================================
 * Hardware init: GPIO + FSMC
 *============================================================================*/
static void lcd_hw_init(void)
{
    GPIO_InitTypeDef gpio_init_struct = {0};

    /* Clocks */
    __HAL_RCC_GPIOD_CLK_ENABLE();
    __HAL_RCC_GPIOE_CLK_ENABLE();
    __HAL_RCC_GPIOG_CLK_ENABLE();
    __HAL_RCC_GPIOB_CLK_ENABLE();

    /* FSMC data/control pins, alternate function push-pull */
    gpio_init_struct.Mode  = GPIO_MODE_AF_PP;
    gpio_init_struct.Pull  = GPIO_NOPULL;
    gpio_init_struct.Speed = GPIO_SPEED_FREQ_HIGH;

    /* PD0, PD1, PD4, PD5, PD8, PD9, PD10, PD14, PD15 */
    gpio_init_struct.Pin = GPIO_PIN_0 | GPIO_PIN_1 | GPIO_PIN_4 | GPIO_PIN_5 |
                           GPIO_PIN_8 | GPIO_PIN_9 | GPIO_PIN_10 |
                           GPIO_PIN_14 | GPIO_PIN_15;
    HAL_GPIO_Init(GPIOD, &gpio_init_struct);

    /* PE7 .. PE15 */
    gpio_init_struct.Pin = GPIO_PIN_7 | GPIO_PIN_8 | GPIO_PIN_9 | GPIO_PIN_10 |
                           GPIO_PIN_11 | GPIO_PIN_12 | GPIO_PIN_13 |
                           GPIO_PIN_14 | GPIO_PIN_15;
    HAL_GPIO_Init(GPIOE, &gpio_init_struct);

    /* PG0 = A10 (RS), PG12 = NE4 (CS) */
    gpio_init_struct.Pin = GPIO_PIN_0 | GPIO_PIN_12;
    HAL_GPIO_Init(GPIOG, &gpio_init_struct);

    /* Backlight */
    gpio_init_struct.Mode  = GPIO_MODE_OUTPUT_PP;
    gpio_init_struct.Pull  = GPIO_NOPULL;
    gpio_init_struct.Speed = GPIO_SPEED_FREQ_LOW;
    gpio_init_struct.Pin   = LCD_BL_PIN;
    HAL_GPIO_Init(LCD_BL_PORT, &gpio_init_struct);
    HAL_GPIO_WritePin(LCD_BL_PORT, LCD_BL_PIN, GPIO_PIN_RESET);
}

static void lcd_fsmc_init(void)
{
    /* FSMC clock (HCLK) */
    RCC->AHBENR |= RCC_AHBENR_FSMCEN;
    (void)RCC->AHBENR;

    /* Bank1 / NE4: 16-bit SRAM, write enabled, separate read and write timing */
    FSMC_Bank1->BTCR[6] = 0x00000000U;                                  /* BCR4: disabled */

    FSMC_Bank1->BTCR[7] = (uint32_t)LCD_READ_ADDSET                     /* BTR4: read timing  */
                        | ((uint32_t)LCD_READ_DATAST << 8)
                        | (0UL << 28);                                  /* access mode A      */

    /* BWTR4 sits at 0x104 + 3 * 8 = 0x11C. CMSIS declares BWTR[] as a plain
     * uint32_t array (stride 4), so every other slot is reserved: index 6. */
    FSMC_Bank1E->BWTR[6] = (uint32_t)LCD_WRITE_ADDSET                   /* BWTR4: write timing */
                         | ((uint32_t)LCD_WRITE_DATAST << 8)
                         | (0UL << 28);                                 /* access mode A       */

    FSMC_Bank1->BTCR[6] = FSMC_BCRx_MBKEN                               /* BCR4: enable bank   */
                        | FSMC_BCRx_MWID_0                              /* 16-bit data bus     */
                        | FSMC_BCRx_WREN
                        | FSMC_BCRx_EXTMOD;
}

/*==============================================================================
 * Panel initialization
 *============================================================================*/
static uint16_t lcd_read_id(void)
{
    uint16_t id;

    lcd_write_cmd(0xD3);                    /* ILI9341: read ID4 */
    (void)lcd_read_data();
    (void)lcd_read_data();
    id  = (uint16_t)(lcd_read_data() << 8);
    id |= (uint16_t)(lcd_read_data() & 0x00FFU);

    if (id != LCD_CHIP_ILI9341)
    {
        lcd_write_cmd(0x04);                /* ST7789V: read ID1 */
        (void)lcd_read_data();
        (void)lcd_read_data();
        id  = (uint16_t)(lcd_read_data() << 8);
        id |= (uint16_t)(lcd_read_data() & 0x00FFU);
    }

    return id;
}

static void lcd_init_ili9341(void)
{
    static const uint8_t cfg_cf[] = {0x00, 0xC1, 0x30};
    static const uint8_t cfg_ed[] = {0x64, 0x03, 0x12, 0x81};
    static const uint8_t cfg_e8[] = {0x85, 0x10, 0x7A};
    static const uint8_t cfg_cb[] = {0x39, 0x2C, 0x00, 0x34, 0x02};
    static const uint8_t cfg_f7[] = {0x20};
    static const uint8_t cfg_ea[] = {0x00, 0x00};
    static const uint8_t cfg_c0[] = {0x1B};
    static const uint8_t cfg_c1[] = {0x01};
    static const uint8_t cfg_c5[] = {0x30, 0x30};
    static const uint8_t cfg_c7[] = {0xB7};
    static const uint8_t cfg_36[] = {0x48};
    static const uint8_t cfg_3a[] = {0x55};
    static const uint8_t cfg_b1[] = {0x00, 0x1A};
    static const uint8_t cfg_b6[] = {0x0A, 0xA2};
    static const uint8_t cfg_f2[] = {0x00};
    static const uint8_t cfg_26[] = {0x01};
    static const uint8_t cfg_e0[] = {0x0F, 0x2A, 0x28, 0x08, 0x0E, 0x08, 0x54, 0xA9,
                                     0x43, 0x0A, 0x0F, 0x00, 0x00, 0x00, 0x00};
    static const uint8_t cfg_e1[] = {0x00, 0x15, 0x17, 0x07, 0x11, 0x06, 0x2B, 0x56,
                                     0x3C, 0x05, 0x10, 0x0F, 0x3F, 0x3F, 0x0F};
    static const uint8_t cfg_2b[] = {0x00, 0x00, 0x01, 0x3F};
    static const uint8_t cfg_2a[] = {0x00, 0x00, 0x00, 0xEF};

    lcd_write_reg(0xCF, cfg_cf, sizeof(cfg_cf));
    lcd_write_reg(0xED, cfg_ed, sizeof(cfg_ed));
    lcd_write_reg(0xE8, cfg_e8, sizeof(cfg_e8));
    lcd_write_reg(0xCB, cfg_cb, sizeof(cfg_cb));
    lcd_write_reg(0xF7, cfg_f7, sizeof(cfg_f7));
    lcd_write_reg(0xEA, cfg_ea, sizeof(cfg_ea));
    lcd_write_reg(0xC0, cfg_c0, sizeof(cfg_c0));
    lcd_write_reg(0xC1, cfg_c1, sizeof(cfg_c1));
    lcd_write_reg(0xC5, cfg_c5, sizeof(cfg_c5));
    lcd_write_reg(0xC7, cfg_c7, sizeof(cfg_c7));
    lcd_write_reg(0x36, cfg_36, sizeof(cfg_36));    /* memory access control  */
    lcd_write_reg(0x3A, cfg_3a, sizeof(cfg_3a));    /* pixel format: RGB565  */
    lcd_write_reg(0xB1, cfg_b1, sizeof(cfg_b1));
    lcd_write_reg(0xB6, cfg_b6, sizeof(cfg_b6));
    lcd_write_reg(0xF2, cfg_f2, sizeof(cfg_f2));
    lcd_write_reg(0x26, cfg_26, sizeof(cfg_26));
    lcd_write_reg(0xE0, cfg_e0, sizeof(cfg_e0));    /* positive gamma        */
    lcd_write_reg(0xE1, cfg_e1, sizeof(cfg_e1));    /* negative gamma        */
    lcd_write_reg(0x2B, cfg_2b, sizeof(cfg_2b));
    lcd_write_reg(0x2A, cfg_2a, sizeof(cfg_2a));

    lcd_write_cmd(0x11);                            /* sleep out */
    HAL_Delay(100);
    lcd_write_cmd(0x29);                            /* display on */
}

static void lcd_init_st7789v(void)
{
    static const uint8_t cfg_36[] = {0x00};
    static const uint8_t cfg_3a[] = {0x05};
    static const uint8_t cfg_b2[] = {0x0C, 0x0C, 0x00, 0x33, 0x33};
    static const uint8_t cfg_b7[] = {0x35};
    static const uint8_t cfg_bb[] = {0x32};
    static const uint8_t cfg_c0[] = {0x0C};
    static const uint8_t cfg_c2[] = {0x01};
    static const uint8_t cfg_c3[] = {0x10};
    static const uint8_t cfg_c4[] = {0x20};
    static const uint8_t cfg_c6[] = {0x0F};
    static const uint8_t cfg_d0[] = {0xA4, 0xA1};
    static const uint8_t cfg_e0[] = {0xD0, 0x00, 0x02, 0x07, 0x0A, 0x28, 0x32, 0x44,
                                     0x42, 0x06, 0x0E, 0x12, 0x14, 0x17};
    static const uint8_t cfg_e1[] = {0xD0, 0x00, 0x02, 0x07, 0x0A, 0x28, 0x31, 0x54,
                                     0x47, 0x0E, 0x1C, 0x17, 0x1B, 0x1E};
    static const uint8_t cfg_2a[] = {0x00, 0x00, 0x00, 0xEF};
    static const uint8_t cfg_2b[] = {0x00, 0x00, 0x01, 0x3F};

    lcd_write_cmd(0x11);                            /* sleep out */
    HAL_Delay(120);

    lcd_write_reg(0x36, cfg_36, sizeof(cfg_36));    /* memory access control  */
    lcd_write_reg(0x3A, cfg_3a, sizeof(cfg_3a));    /* pixel format: RGB565  */
    lcd_write_reg(0xB2, cfg_b2, sizeof(cfg_b2));
    lcd_write_reg(0xB7, cfg_b7, sizeof(cfg_b7));
    lcd_write_reg(0xBB, cfg_bb, sizeof(cfg_bb));
    lcd_write_reg(0xC0, cfg_c0, sizeof(cfg_c0));
    lcd_write_reg(0xC2, cfg_c2, sizeof(cfg_c2));
    lcd_write_reg(0xC3, cfg_c3, sizeof(cfg_c3));
    lcd_write_reg(0xC4, cfg_c4, sizeof(cfg_c4));
    lcd_write_reg(0xC6, cfg_c6, sizeof(cfg_c6));
    lcd_write_reg(0xD0, cfg_d0, sizeof(cfg_d0));
    lcd_write_reg(0xE0, cfg_e0, sizeof(cfg_e0));
    lcd_write_reg(0xE1, cfg_e1, sizeof(cfg_e1));
    lcd_write_reg(0x2A, cfg_2a, sizeof(cfg_2a));
    lcd_write_reg(0x2B, cfg_2b, sizeof(cfg_2b));

    lcd_write_cmd(0x29);                            /* display on */
}

/*==============================================================================
 * Public API
 *============================================================================*/
void LCD_BackLight(uint8_t on)
{
    HAL_GPIO_WritePin(LCD_BL_PORT, LCD_BL_PIN, on ? GPIO_PIN_SET : GPIO_PIN_RESET);
}

void LCD_DisplayOn(void)
{
    lcd_write_cmd(0x29);
}

void LCD_DisplayOff(void)
{
    lcd_write_cmd(0x28);
}

uint16_t LCD_ReadID(void)
{
    return g_lcd_id;
}

uint16_t LCD_GetWidth(void)
{
    return g_lcd_width;
}

uint16_t LCD_GetHeight(void)
{
    return g_lcd_height;
}

uint8_t LCD_GetDir(void)
{
    return g_lcd_dir;
}

void LCD_SetDir(uint8_t dir)
{
    uint16_t madctl;

    switch (dir)
    {
        case LCD_DIR_0:   g_lcd_width = LCD_PANEL_WIDTH;  g_lcd_height = LCD_PANEL_HEIGHT; madctl = 0x08; break;
        case LCD_DIR_90:  g_lcd_width = LCD_PANEL_HEIGHT; g_lcd_height = LCD_PANEL_WIDTH;  madctl = 0x68; break;
        case LCD_DIR_180: g_lcd_width = LCD_PANEL_WIDTH;  g_lcd_height = LCD_PANEL_HEIGHT; madctl = 0xC8; break;
        case LCD_DIR_270: g_lcd_width = LCD_PANEL_HEIGHT; g_lcd_height = LCD_PANEL_WIDTH;  madctl = 0xA8; break;
        default: return;
    }

    g_lcd_dir = dir;

    lcd_write_cmd(0x36);
    lcd_write_data(madctl);

    lcd_set_window(0, 0, g_lcd_width, g_lcd_height);
}

/* Common part of both init entry points (hardware must already be up). */
static void lcd_startup(uint16_t id)
{
    g_lcd_id = id;

    if (id == LCD_CHIP_ST7789V)
    {
        lcd_init_st7789v();
    }
    else
    {
        lcd_init_ili9341();
    }

    LCD_SetDir(LCD_DIR_0);
    LCD_Clear(LCD_WHITE);
    LCD_DisplayOn();
    LCD_BackLight(1);
}

void LCD_InitAs(uint16_t id)
{
    lcd_hw_init();
    lcd_fsmc_init();
    HAL_Delay(10);

    lcd_startup((id == LCD_CHIP_ST7789V) ? LCD_CHIP_ST7789V : LCD_CHIP_ILI9341);
}

uint16_t LCD_Init(void)
{
    uint16_t id;

    lcd_hw_init();
    lcd_fsmc_init();
    HAL_Delay(10);

    id = lcd_read_id();
    if ((id != LCD_CHIP_ILI9341) && (id != LCD_CHIP_ST7789V))
    {
        return 0;                           /* unknown panel: keep backlight off */
    }

    lcd_startup(id);
    return id;
}

void LCD_Clear(uint16_t color)
{
    LCD_Fill(0, 0, g_lcd_width, g_lcd_height, color);
}

void LCD_Fill(uint16_t x, uint16_t y, uint16_t w, uint16_t h, uint16_t color)
{
    uint32_t count;

    if ((w == 0U) || (h == 0U) || (x >= g_lcd_width) || (y >= g_lcd_height))
    {
        return;
    }
    if ((uint32_t)x + w > g_lcd_width)
    {
        w = (uint16_t)(g_lcd_width - x);
    }
    if ((uint32_t)y + h > g_lcd_height)
    {
        h = (uint16_t)(g_lcd_height - y);
    }

    lcd_set_window(x, y, w, h);

    count = (uint32_t)w * h;
    while (count--)
    {
        lcd_write_data(color);
    }
}

void LCD_DrawPoint(uint16_t x, uint16_t y, uint16_t color)
{
    if ((x >= g_lcd_width) || (y >= g_lcd_height))
    {
        return;
    }

    lcd_set_window(x, y, 1, 1);
    lcd_write_data(color);
}

uint16_t LCD_ReadPoint(uint16_t x, uint16_t y)
{
    uint16_t color;

    if ((x >= g_lcd_width) || (y >= g_lcd_height))
    {
        return 0;
    }

    lcd_write_cmd(0x2A);
    lcd_write_data((uint16_t)(x >> 8));
    lcd_write_data((uint16_t)(x & 0xFFU));
    lcd_write_data((uint16_t)(x >> 8));
    lcd_write_data((uint16_t)(x & 0xFFU));

    lcd_write_cmd(0x2B);
    lcd_write_data((uint16_t)(y >> 8));
    lcd_write_data((uint16_t)(y & 0xFFU));
    lcd_write_data((uint16_t)(y >> 8));
    lcd_write_data((uint16_t)(y & 0xFFU));

    lcd_write_cmd(0x2E);                    /* memory read */

    (void)lcd_read_data();                  /* dummy */
    color = lcd_read_data();

    return color;
}

void LCD_DrawLine(uint16_t x1, uint16_t y1, uint16_t x2, uint16_t y2, uint16_t color)
{
    int16_t dx = (x1 < x2) ? (int16_t)(x2 - x1) : (int16_t)(x1 - x2);
    int16_t dy = (y1 < y2) ? (int16_t)(y2 - y1) : (int16_t)(y1 - y2);
    int16_t sx = (x1 < x2) ? 1 : -1;
    int16_t sy = (y1 < y2) ? 1 : -1;
    int16_t err = (int16_t)(dx - dy);
    int16_t e2;

    for (;;)
    {
        LCD_DrawPoint(x1, y1, color);
        if ((x1 == x2) && (y1 == y2))
        {
            break;
        }

        e2 = (int16_t)(err << 1);
        if (e2 > -dy)
        {
            err = (int16_t)(err - dy);
            x1  = (uint16_t)(x1 + sx);
        }
        if (e2 < dx)
        {
            err = (int16_t)(err + dx);
            y1  = (uint16_t)(y1 + sy);
        }
    }
}

void LCD_DrawRect(uint16_t x, uint16_t y, uint16_t w, uint16_t h, uint16_t color)
{
    if ((w == 0U) || (h == 0U))
    {
        return;
    }

    LCD_DrawLine(x, y, (uint16_t)(x + w - 1U), y, color);
    LCD_DrawLine(x, (uint16_t)(y + h - 1U), (uint16_t)(x + w - 1U), (uint16_t)(y + h - 1U), color);
    LCD_DrawLine(x, y, x, (uint16_t)(y + h - 1U), color);
    LCD_DrawLine((uint16_t)(x + w - 1U), y, (uint16_t)(x + w - 1U), (uint16_t)(y + h - 1U), color);
}

void LCD_DrawCircle(uint16_t x, uint16_t y, uint16_t r, uint16_t color)
{
    int16_t cx = -(int16_t)r;
    int16_t cy = 0;
    int16_t err = (int16_t)(2 - 2 * (int16_t)r);
    int16_t err2;

    do
    {
        LCD_DrawPoint((uint16_t)(x - cx), (uint16_t)(y + cy), color);
        LCD_DrawPoint((uint16_t)(x + cx), (uint16_t)(y + cy), color);
        LCD_DrawPoint((uint16_t)(x + cx), (uint16_t)(y - cy), color);
        LCD_DrawPoint((uint16_t)(x - cx), (uint16_t)(y - cy), color);

        err2 = err;
        if (err2 <= cy)
        {
            cy++;
            err = (int16_t)(err + (cy * 2 + 1));
            if ((-cx == cy) && (err2 <= cx))
            {
                err2 = 0;
            }
        }
        if (err2 > cx)
        {
            cx++;
            err = (int16_t)(err + (cx * 2 + 1));
        }
    } while (cx <= 0);
}

void LCD_ShowImage(uint16_t x, uint16_t y, uint16_t w, uint16_t h, const uint16_t *img)
{
    uint32_t count;

    if ((w == 0U) || (h == 0U) || (img == 0) ||
        ((uint32_t)x + w > g_lcd_width) || ((uint32_t)y + h > g_lcd_height))
    {
        return;
    }

    lcd_set_window(x, y, w, h);

    count = (uint32_t)w * h;
    while (count--)
    {
        lcd_write_data(*img++);
    }
}

/*==============================================================================
 * Text
 *============================================================================*/
void LCD_ShowChar(uint16_t x, uint16_t y, char ch, uint16_t fg, uint16_t bg, uint8_t size)
{
    const uint8_t *font;
    uint16_t font_w = (uint16_t)(8U * size);
    uint16_t font_h = (uint16_t)(16U * size);
    uint16_t row;
    uint8_t  col;
    uint8_t  scale_x;
    uint8_t  scale_y;
    uint16_t color;

    if (size == 0U)
    {
        size = 1;
        font_w = 8;
        font_h = 16;
    }
    if ((ch < ' ') || (ch > '~'))
    {
        ch = ' ';
    }
    if ((uint32_t)x + font_w > g_lcd_width || (uint32_t)y + font_h > g_lcd_height)
    {
        return;
    }

    font = lcd_font8x16[ch - ' '];

    lcd_set_window(x, y, font_w, font_h);

    for (row = 0; row < 16U; row++)
    {
        for (scale_y = 0; scale_y < size; scale_y++)
        {
            for (col = 0; col < 8U; col++)
            {
                uint8_t bits = font[col * 2U + (row >> 3)];
                color = ((bits >> (7U - (row & 7U))) & 0x01U) ? fg : bg;

                for (scale_x = 0; scale_x < size; scale_x++)
                {
                    lcd_write_data(color);
                }
            }
        }
    }
}

void LCD_ShowString(uint16_t x, uint16_t y, const char *str, uint16_t fg, uint16_t bg, uint8_t size)
{
    uint16_t x_start = x;
    uint16_t char_w;
    uint16_t char_h;

    if (size == 0U)
    {
        size = 1;
    }
    char_w = (uint16_t)(8U * size);
    char_h = (uint16_t)(16U * size);

    while (*str != '\0')
    {
        if (*str == '\n')
        {
            x = x_start;
            y = (uint16_t)(y + char_h);
        }
        else if (*str != '\r')
        {
            if ((uint32_t)x + char_w > g_lcd_width)
            {
                x = x_start;
                y = (uint16_t)(y + char_h);
            }
            if ((uint32_t)y + char_h > g_lcd_height)
            {
                break;
            }
            LCD_ShowChar(x, y, *str, fg, bg, size);
            x = (uint16_t)(x + char_w);
        }
        str++;
    }
}

void LCD_ShowNum(uint16_t x, uint16_t y, uint32_t num, uint8_t len,
                 uint16_t fg, uint16_t bg, uint8_t size)
{
    uint8_t i;
    uint8_t step = (uint8_t)(8U * ((size == 0U) ? 1U : size));

    for (i = 0; i < len; i++)
    {
        uint32_t div = 1;
        uint8_t  k;

        for (k = (uint8_t)(len - i - 1U); k > 0U; k--)
        {
            div *= 10U;
        }

        LCD_ShowChar((uint16_t)(x + i * step), y, (char)('0' + (num / div) % 10U),
                     fg, bg, size);
    }
}

void LCD_Test(void)
{
    static const uint16_t colors[8] = {
        LCD_RED, LCD_GREEN, LCD_BLUE, LCD_YELLOW,
        LCD_CYAN, LCD_MAGENTA, LCD_WHITE, LCD_BLACK
    };
    uint16_t w = g_lcd_width;
    uint16_t h = g_lcd_height;
    uint16_t band = (uint16_t)(h / 8U);
    uint8_t  i;
    char     id_text[7];
    uint16_t id = g_lcd_id;

    for (i = 0; i < 8U; i++)
    {
        LCD_Fill(0, (uint16_t)(i * band), w, band, colors[i]);
    }

    LCD_Fill(0, (uint16_t)(h / 2U - 30U), w, 60, LCD_BLACK);
    LCD_ShowString(8, (uint16_t)(h / 2U - 24U), "ATK-MD0280 OK", LCD_WHITE, LCD_BLACK, 2);

    id_text[0] = 'I';
    id_text[1] = 'D';
    id_text[2] = ' ';
    id_text[3] = "0123456789ABCDEF"[(id >> 12) & 0x0F];
    id_text[4] = "0123456789ABCDEF"[(id >> 8) & 0x0F];
    id_text[5] = "0123456789ABCDEF"[(id >> 4) & 0x0F];
    id_text[6] = '\0';
    LCD_ShowString(8, (uint16_t)(h / 2U + 8U), id_text, LCD_YELLOW, LCD_BLACK, 1);
}

/*==============================================================================
 * Resistive touch (XPT2046), optional
 *============================================================================*/
#if (LCD_USE_TOUCH != 0)

#define LCD_TOUCH_PEN_PORT      GPIOF
#define LCD_TOUCH_PEN_PIN       GPIO_PIN_10
#define LCD_TOUCH_TCS_PORT      GPIOF
#define LCD_TOUCH_TCS_PIN       GPIO_PIN_11
#define LCD_TOUCH_CLK_PORT      GPIOB
#define LCD_TOUCH_CLK_PIN       GPIO_PIN_1
#define LCD_TOUCH_MOSI_PORT     GPIOF
#define LCD_TOUCH_MOSI_PIN      GPIO_PIN_9
#define LCD_TOUCH_MISO_PORT     GPIOB
#define LCD_TOUCH_MISO_PIN      GPIO_PIN_2

#define LCD_TOUCH_PEN()         HAL_GPIO_ReadPin(LCD_TOUCH_PEN_PORT, LCD_TOUCH_PEN_PIN)

#define LCD_TOUCH_TCS(x)        HAL_GPIO_WritePin(LCD_TOUCH_TCS_PORT, LCD_TOUCH_TCS_PIN, \
                                                  (x) ? GPIO_PIN_SET : GPIO_PIN_RESET)
#define LCD_TOUCH_CLK(x)        HAL_GPIO_WritePin(LCD_TOUCH_CLK_PORT, LCD_TOUCH_CLK_PIN, \
                                                  (x) ? GPIO_PIN_SET : GPIO_PIN_RESET)
#define LCD_TOUCH_MOSI(x)       HAL_GPIO_WritePin(LCD_TOUCH_MOSI_PORT, LCD_TOUCH_MOSI_PIN, \
                                                  (x) ? GPIO_PIN_SET : GPIO_PIN_RESET)
#define LCD_TOUCH_MISO()        HAL_GPIO_ReadPin(LCD_TOUCH_MISO_PORT, LCD_TOUCH_MISO_PIN)

#define LCD_TOUCH_CMD_X         0xD0U
#define LCD_TOUCH_CMD_Y         0x90U

/* 消抖参数：电平必须连续保持这么久才算一次有效的按下/松开 */
#define LCD_TOUCH_DEBOUNCE_MS   20U
/* 按下后等待压力稳定再采样（避免刚开始接触时读数跳变） */
#define LCD_TOUCH_SETTLE_MS     30U

static struct
{
    float    x_scale;
    float    y_scale;
    uint16_t x_center;
    uint16_t y_center;
} g_touch = {
    LCD_TOUCH_DEF_X_SCALE,
    LCD_TOUCH_DEF_Y_SCALE,
    LCD_TOUCH_DEF_X_CENTER,
    LCD_TOUCH_DEF_Y_CENTER
};

/* 扫描时用于按下消抖的状态 */
static uint8_t  g_touch_pressed;
static uint32_t g_touch_press_tick;

static void lcd_udelay(uint32_t us)
{
    uint32_t n = us * 12U;

    while (n--)
    {
        __NOP();
    }
}

void LCD_TouchHwInit(void)
{
    GPIO_InitTypeDef gpio_init_struct = {0};

    __HAL_RCC_GPIOB_CLK_ENABLE();
    __HAL_RCC_GPIOF_CLK_ENABLE();

    gpio_init_struct.Pin   = LCD_TOUCH_MISO_PIN;
    gpio_init_struct.Mode  = GPIO_MODE_INPUT;
    gpio_init_struct.Pull  = GPIO_PULLUP;
    gpio_init_struct.Speed = GPIO_SPEED_FREQ_HIGH;
    HAL_GPIO_Init(LCD_TOUCH_MISO_PORT, &gpio_init_struct);

    gpio_init_struct.Pin   = LCD_TOUCH_PEN_PIN;
    gpio_init_struct.Mode  = GPIO_MODE_INPUT;
    gpio_init_struct.Pull  = GPIO_PULLUP;
    HAL_GPIO_Init(LCD_TOUCH_PEN_PORT, &gpio_init_struct);

    gpio_init_struct.Mode = GPIO_MODE_OUTPUT_PP;
    gpio_init_struct.Pull = GPIO_NOPULL;

    gpio_init_struct.Pin = LCD_TOUCH_MOSI_PIN;
    HAL_GPIO_Init(LCD_TOUCH_MOSI_PORT, &gpio_init_struct);
    gpio_init_struct.Pin = LCD_TOUCH_TCS_PIN;
    HAL_GPIO_Init(LCD_TOUCH_TCS_PORT, &gpio_init_struct);
    gpio_init_struct.Pin = LCD_TOUCH_CLK_PIN;
    HAL_GPIO_Init(LCD_TOUCH_CLK_PORT, &gpio_init_struct);

    LCD_TOUCH_TCS(1);
    LCD_TOUCH_CLK(0);
}

static void lcd_touch_spi_write(uint8_t dat)
{
    uint8_t i;

    for (i = 0; i < 8U; i++)
    {
        LCD_TOUCH_MOSI((dat & 0x80U) ? 1 : 0);
        dat = (uint8_t)(dat << 1);
        LCD_TOUCH_CLK(0);
        lcd_udelay(1);
        LCD_TOUCH_CLK(1);
    }
}

static uint16_t lcd_touch_spi_read(uint8_t cmd)
{
    uint16_t dat = 0;
    uint8_t  i;

    LCD_TOUCH_CLK(0);
    LCD_TOUCH_MOSI(0);
    LCD_TOUCH_TCS(0);

    lcd_touch_spi_write(cmd);
    lcd_udelay(6);

    LCD_TOUCH_CLK(0);
    lcd_udelay(1);
    LCD_TOUCH_CLK(1);
    lcd_udelay(1);
    LCD_TOUCH_CLK(0);

    for (i = 0; i < 16U; i++)
    {
        dat = (uint16_t)(dat << 1);
        LCD_TOUCH_CLK(0);
        lcd_udelay(1);
        LCD_TOUCH_CLK(1);
        if (LCD_TOUCH_MISO() != 0)
        {
            dat++;
        }
    }

    dat >>= 4;
    LCD_TOUCH_TCS(1);

    return dat;
}

/* Average 5 samples, drop the highest and the lowest one. */
static uint16_t lcd_touch_adc(uint8_t cmd)
{
    uint16_t v[5];
    uint16_t tmp;
    uint16_t sum = 0;
    uint8_t  i;
    uint8_t  j;

    for (i = 0; i < 5U; i++)
    {
        v[i] = lcd_touch_spi_read(cmd);
    }

    for (i = 0; i < 4U; i++)
    {
        for (j = (uint8_t)(i + 1U); j < 5U; j++)
        {
            if (v[i] > v[j])
            {
                tmp  = v[i];
                v[i] = v[j];
                v[j] = tmp;
            }
        }
    }

    for (i = 1; i < 4U; i++)
    {
        sum = (uint16_t)(sum + v[i]);
    }

    return (uint16_t)(sum / 3U);
}

/* Read until two consecutive results are close enough. */
static uint16_t lcd_touch_adc_stable(uint8_t cmd)
{
    uint16_t a;
    uint16_t b;
    uint8_t  retry = 20;

    do
    {
        a = lcd_touch_adc(cmd);
        b = lcd_touch_adc(cmd);
        if (a > b)
        {
            if ((a - b) <= 50U)
            {
                break;
            }
        }
        else
        {
            if ((b - a) <= 50U)
            {
                break;
            }
        }
    } while (--retry != 0U);

    return (uint16_t)((a + b) >> 1);
}

static void lcd_touch_draw_cross(uint16_t x, uint16_t y, uint16_t color)
{
    LCD_DrawLine((uint16_t)(x - 12U), y, (uint16_t)(x + 12U), y, color);
    LCD_DrawLine(x, (uint16_t)(y - 12U), x, (uint16_t)(y + 12U), color);
    LCD_DrawCircle(x, y, 6, color);
    LCD_DrawCircle(x, y, 7, color);
}

/* 等待一次消抖后的按下：PEN 必须连续保持低电平 debounce_ms 才算数。
 * 返回 1 表示按下有效，0 表示超时。 */
static uint8_t lcd_touch_wait_press(uint16_t debounce_ms, uint32_t timeout_ms)
{
    uint32_t start = HAL_GetTick();
    uint32_t low_since;

    for (;;)
    {
        while (LCD_TOUCH_PEN() != 0)            /* 先等第一次接触 */
        {
            if ((HAL_GetTick() - start) > timeout_ms)
            {
                return 0;
            }
        }

        low_since = HAL_GetTick();              /* 再要求电平保持稳定 */
        while ((HAL_GetTick() - low_since) < debounce_ms)
        {
            if (LCD_TOUCH_PEN() != 0)
            {
                break;                          /* 抖动，重新等 */
            }
        }

        if (LCD_TOUCH_PEN() == 0)
        {
            return 1;
        }
    }
}

/* 等待一次消抖后的松开：PEN 必须连续保持高电平 debounce_ms 才算数。 */
static uint8_t lcd_touch_wait_release(uint16_t debounce_ms, uint32_t timeout_ms)
{
    uint32_t start = HAL_GetTick();
    uint32_t high_since;

    for (;;)
    {
        while (LCD_TOUCH_PEN() == 0)            /* 先等松开 */
        {
            if ((HAL_GetTick() - start) > timeout_ms)
            {
                return 0;
            }
        }

        high_since = HAL_GetTick();             /* 再要求电平保持稳定 */
        while ((HAL_GetTick() - high_since) < debounce_ms)
        {
            if (LCD_TOUCH_PEN() == 0)
            {
                break;                          /* 抖动，重新等 */
            }
        }

        if (LCD_TOUCH_PEN() != 0)
        {
            return 1;
        }
    }
}

/* 显示 4 位小数，避免为了打印浮点引入 printf */
static void lcd_show_float4(uint16_t x, uint16_t y, float v,
                            uint16_t fg, uint16_t bg, uint8_t size)
{
    char     buf[10];
    uint32_t t;
    uint32_t ip;
    uint8_t  i = 0;

    if (v < 0.0f)
    {
        v = -v;
    }

    t  = (uint32_t)((v * 10000.0f) + 0.5f);
    ip = t / 10000U;

    if (ip >= 100U)
    {
        buf[i++] = (char)('0' + (ip / 100U) % 10U);
    }
    if (ip >= 10U)
    {
        buf[i++] = (char)('0' + (ip / 10U) % 10U);
    }
    buf[i++] = (char)('0' + (ip % 10U));
    buf[i++] = '.';
    buf[i++] = (char)('0' + ((t / 1000U) % 10U));
    buf[i++] = (char)('0' + ((t / 100U) % 10U));
    buf[i++] = (char)('0' + ((t / 10U) % 10U));
    buf[i++] = (char)('0' + (t % 10U));
    buf[i]   = '\0';

    LCD_ShowString(x, y, buf, fg, bg, size);
}

void LCD_TouchGetCal(lcd_touch_cal_t *cal)
{
    if (cal == 0)
    {
        return;
    }

    cal->x_scale  = g_touch.x_scale;
    cal->y_scale  = g_touch.y_scale;
    cal->x_center = g_touch.x_center;
    cal->y_center = g_touch.y_center;
}

void LCD_TouchSetCal(const lcd_touch_cal_t *cal)
{
    if (cal == 0)
    {
        return;
    }

    g_touch.x_scale  = cal->x_scale;
    g_touch.y_scale  = cal->y_scale;
    g_touch.x_center = cal->x_center;
    g_touch.y_center = cal->y_center;
}

/* 把当前校准参数显示在屏上（校准结束后会自动调用，也可以随时手动调） */
void LCD_TouchShowCal(void)
{
    LCD_Clear(LCD_WHITE);
    LCD_ShowString(6, 6, "Calibration OK", LCD_GREEN, LCD_WHITE, 2);

    LCD_ShowString(6, 44, "x_scale :", LCD_BLACK, LCD_WHITE, 1);
    lcd_show_float4(90, 44, g_touch.x_scale, LCD_RED, LCD_WHITE, 1);

    LCD_ShowString(6, 60, "y_scale :", LCD_BLACK, LCD_WHITE, 1);
    lcd_show_float4(90, 60, g_touch.y_scale, LCD_RED, LCD_WHITE, 1);

    LCD_ShowString(6, 76, "x_center:", LCD_BLACK, LCD_WHITE, 1);
    LCD_ShowNum(90, 76, g_touch.x_center, 4, LCD_RED, LCD_WHITE, 1);

    LCD_ShowString(6, 92, "y_center:", LCD_BLACK, LCD_WHITE, 1);
    LCD_ShowNum(90, 92, g_touch.y_center, 4, LCD_RED, LCD_WHITE, 1);

    LCD_ShowString(6, 120, "Replace LCD_TOUCH_DEF_*", LCD_BLUE, LCD_WHITE, 1);
    LCD_ShowString(6, 136, "in lcd.h with these", LCD_BLUE, LCD_WHITE, 1);
}

/* 5 点校准：在屏上依次画十字，等待用户点击；结束后把新参数显示 8 秒并返回。
 * 阻塞函数，直接调用即可；校准失败时保留原参数。 */
lcd_touch_cal_t LCD_TouchCalibrate(void)
{
    lcd_touch_cal_t cal;
    uint16_t px[5];
    uint16_t py[5];
    int16_t  d1;
    int16_t  d2;
    int16_t  d3;
    int16_t  d4;
    double   x_fac;
    double   y_fac;
    uint8_t  i;
    uint16_t w = g_lcd_width;
    uint16_t h = g_lcd_height;

    LCD_Clear(LCD_WHITE);
    LCD_ShowString(10, 10, "Touch the cross with a stylus", LCD_RED, LCD_WHITE, 1);
    LCD_ShowString(10, 30, "Calibrating...", LCD_BLUE, LCD_WHITE, 1);

    /* 5 points: top-left, top-right, bottom-left, bottom-right, center */
    {
        uint16_t pts_x[5];
        uint16_t pts_y[5];

        pts_x[0] = 20;               pts_y[0] = 20;
        pts_x[1] = (uint16_t)(w - 20U); pts_y[1] = 20;
        pts_x[2] = 20;               pts_y[2] = (uint16_t)(h - 20U);
        pts_x[3] = (uint16_t)(w - 20U); pts_y[3] = (uint16_t)(h - 20U);
        pts_x[4] = (uint16_t)(w / 2U);  pts_y[4] = (uint16_t)(h / 2U);

        for (i = 0; i < 5U; i++)
        {
            uint8_t ok = 0;

            lcd_touch_draw_cross(pts_x[i], pts_y[i], LCD_RED);

            while (ok == 0)
            {
                /* 消抖后的按下；15 s 内取不到有效触摸就报错退出 */
                if (lcd_touch_wait_press(LCD_TOUCH_DEBOUNCE_MS, 15000U) == 0)
                {
                    /* 保持原来的校准参数不变 */
                    LCD_Clear(LCD_WHITE);
                    LCD_ShowString(10, 10, "Touch not detected!", LCD_RED, LCD_WHITE, 1);
                    LCD_ShowString(10, 30, "Old calibration kept", LCD_RED, LCD_WHITE, 1);
                    HAL_Delay(1500);
                    LCD_Clear(LCD_WHITE);
                    LCD_TouchGetCal(&cal);
                    return cal;
                }

                /* 等压力稳定再采样 */
                HAL_Delay(LCD_TOUCH_SETTLE_MS);

                if (LCD_TOUCH_PEN() == 0)
                {
                    px[i] = lcd_touch_adc_stable(LCD_TOUCH_CMD_X);
                    py[i] = lcd_touch_adc_stable(LCD_TOUCH_CMD_Y);

                    /* 采样期间松手就丢掉这次，重新来 */
                    if (LCD_TOUCH_PEN() == 0)
                    {
                        ok = 1;
                    }
                }

                /* 等消抖后的松开，避免抖动被当成下一次按下 */
                (void)lcd_touch_wait_release(LCD_TOUCH_DEBOUNCE_MS, 3000U);
            }

            lcd_touch_draw_cross(pts_x[i], pts_y[i], LCD_WHITE);
        }
    }

    d1 = (int16_t)(px[1] - px[0]);   /* X distance on the top edge    */
    d3 = (int16_t)(px[3] - px[2]);   /* X distance on the bottom edge */
    d2 = (int16_t)(py[3] - py[1]);   /* Y distance on the right edge  */
    d4 = (int16_t)(py[2] - py[0]);   /* Y distance on the left edge   */

    if ((d1 == 0) || (d2 == 0) || (d3 == 0) || (d4 == 0))
    {
        /* 数据无效，保留原来的校准参数 */
        LCD_Clear(LCD_WHITE);
        LCD_ShowString(10, 10, "Calibration failed", LCD_RED, LCD_WHITE, 1);
        LCD_ShowString(10, 30, "Old calibration kept", LCD_RED, LCD_WHITE, 1);
        HAL_Delay(1500);
        LCD_Clear(LCD_WHITE);
        LCD_TouchGetCal(&cal);
        return cal;
    }

    x_fac = (double)(d1 + d3) / (2.0 * (double)(w - 40U));
    y_fac = (double)(d2 + d4) / (2.0 * (double)(h - 40U));

    if (x_fac < 0.0)
    {
        x_fac = -x_fac;
    }
    if (y_fac < 0.0)
    {
        y_fac = -y_fac;
    }

    g_touch.x_scale  = (float)(1.0 / x_fac);
    g_touch.y_scale  = (float)(1.0 / y_fac);
    g_touch.x_center = px[4];
    g_touch.y_center = py[4];

    /* 把结果打印在屏上，方便抄回 lcd.h 的 LCD_TOUCH_DEF_* */
    LCD_TouchShowCal();

    HAL_Delay(8000);
    LCD_Clear(LCD_WHITE);

    LCD_TouchGetCal(&cal);
    return cal;
}

/* 触摸初始化 = 硬件初始化 + 一次 5 点校准 */
void LCD_TouchInit(void)
{
    LCD_TouchHwInit();
    (void)LCD_TouchCalibrate();
}

/* 只判断有没有按下，不依赖校准参数（直接读 PEN 引脚） */
uint8_t LCD_TouchIsDown(void)
{
    return (LCD_TOUCH_PEN() == 0) ? 1U : 0U;
}

uint8_t LCD_TouchScan(uint16_t *x, uint16_t *y)
{
    uint16_t x_adc;
    uint16_t y_adc;
    int16_t  x_raw;
    int16_t  y_raw;

    if (LCD_TOUCH_PEN() != 0)
    {
        g_touch_pressed = 0;                /* 松开，等下一次按下重新消抖 */
        return 0;
    }

    if ((g_touch.x_scale == 0.0f) || (g_touch.y_scale == 0.0f))
    {
        return 0;                           /* not calibrated yet */
    }

    /* 按下消抖：电平必须连续保持 LCD_TOUCH_DEBOUNCE_MS 才上报，
     * 抖动/毛刺在这一步就被丢掉。 */
    if (g_touch_pressed == 0)
    {
        g_touch_pressed    = 1;
        g_touch_press_tick = HAL_GetTick();
        return 0;
    }

    if ((HAL_GetTick() - g_touch_press_tick) < LCD_TOUCH_DEBOUNCE_MS)
    {
        return 0;
    }

    x_adc = lcd_touch_adc_stable(LCD_TOUCH_CMD_X);
    y_adc = lcd_touch_adc_stable(LCD_TOUCH_CMD_Y);

    /* 采样期间松手，这次结果作废 */
    if (LCD_TOUCH_PEN() != 0)
    {
        g_touch_pressed = 0;
        return 0;
    }

    x_raw = (int16_t)((float)((int16_t)(x_adc - g_touch.x_center)) * g_touch.x_scale
                      + (float)(LCD_PANEL_WIDTH / 2U));
    y_raw = (int16_t)((float)((int16_t)(y_adc - g_touch.y_center)) * g_touch.y_scale
                      + (float)(LCD_PANEL_HEIGHT / 2U));

    if (x_raw < 0)
    {
        x_raw = 0;
    }
    if (y_raw < 0)
    {
        y_raw = 0;
    }
    if (x_raw >= LCD_PANEL_WIDTH)
    {
        x_raw = LCD_PANEL_WIDTH - 1;
    }
    if (y_raw >= LCD_PANEL_HEIGHT)
    {
        y_raw = LCD_PANEL_HEIGHT - 1;
    }

    switch (g_lcd_dir)
    {
        case LCD_DIR_0:
            *x = (uint16_t)x_raw;
            *y = (uint16_t)y_raw;
            break;

        case LCD_DIR_90:
            *x = (uint16_t)y_raw;
            *y = (uint16_t)(LCD_PANEL_WIDTH - 1 - x_raw);
            break;

        case LCD_DIR_180:
            *x = (uint16_t)(LCD_PANEL_WIDTH - 1 - x_raw);
            *y = (uint16_t)(LCD_PANEL_HEIGHT - 1 - y_raw);
            break;

        case LCD_DIR_270:
            *x = (uint16_t)(LCD_PANEL_HEIGHT - 1 - y_raw);
            *y = (uint16_t)x_raw;
            break;

        default:
            *x = (uint16_t)x_raw;
            *y = (uint16_t)y_raw;
            break;
    }

    return 1;
}

#endif /* LCD_USE_TOUCH */
