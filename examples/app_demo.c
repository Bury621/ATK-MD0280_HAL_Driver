/**
 ******************************************************************************
 * @file    app_demo.c
 * @brief   把所有显示 API 依次演示一遍
 *
 *          上电后自动播放：
 *            信息页 -> 图形 -> 文字 -> 图片 -> 读点 -> 自检彩条 -> 四个方向
 *          最后停在结束页，并每 500 ms 刷新一次计数。
 *
 *          触摸演示需要把 lcd.h 的 LCD_USE_TOUCH 设为 1。
 ******************************************************************************
 */

#include "app.h"
#include "lcd.h"

/* 演示用的 64x64 RGB565 图片缓冲区（8 KB） */
static uint16_t g_image[64 * 64];

/* 十六进制显示，ShowNum 只能显示十进制，这里拼一个字符串 */
static void ShowHex(uint16_t x, uint16_t y, uint16_t value,
                    uint16_t fg, uint16_t bg, uint8_t size)
{
    static const char hex[] = "0123456789ABCDEF";
    char buf[5];

    buf[0] = hex[(value >> 12) & 0x0FU];
    buf[1] = hex[(value >> 8) & 0x0FU];
    buf[2] = hex[(value >> 4) & 0x0FU];
    buf[3] = hex[value & 0x0FU];
    buf[4] = '\0';

    LCD_ShowString(x, y, buf, fg, bg, size);
}

/* 基本信息页：ID、分辨率、方向 */
static void Demo_Info(uint16_t id)
{
    LCD_Clear(LCD_WHITE);
    LCD_ShowString(10, 10, "ATK-MD0280", LCD_RED, LCD_WHITE, 2);

    LCD_ShowString(10, 50, "ID: 0x", LCD_BLACK, LCD_WHITE, 1);
    ShowHex(60, 50, id, LCD_BLACK, LCD_WHITE, 1);

    LCD_ShowString(10, 70, "W=", LCD_BLACK, LCD_WHITE, 1);
    LCD_ShowNum(30, 70, LCD_GetWidth(), 3, LCD_BLACK, LCD_WHITE, 1);

    LCD_ShowString(80, 70, "H=", LCD_BLACK, LCD_WHITE, 1);
    LCD_ShowNum(100, 70, LCD_GetHeight(), 3, LCD_BLACK, LCD_WHITE, 1);

    LCD_ShowString(10, 90, "DIR=", LCD_BLACK, LCD_WHITE, 1);
    LCD_ShowNum(50, 90, LCD_GetDir(), 1, LCD_BLACK, LCD_WHITE, 1);

    /* ShowChar 单独演示：一个放大 4 倍的 'A'（32x64） */
    LCD_ShowChar(10, 120, 'A', LCD_BLUE, LCD_WHITE, 4);
}

/* 绘图接口：Fill / DrawRect / DrawCircle / DrawLine / DrawPoint */
static void Demo_Graphics(void)
{
    uint16_t i;

    LCD_Clear(LCD_WHITE);
    LCD_ShowString(0, 0, "Graphics", LCD_BLUE, LCD_WHITE, 2);

    LCD_Fill(10, 40, 100, 60, LCD_RED);
    LCD_DrawRect(10, 40, 100, 60, LCD_BLACK);

    LCD_Fill(130, 40, 100, 60, LCD_GREEN);
    LCD_DrawRect(130, 40, 100, 60, LCD_BLACK);

    LCD_DrawCircle(70, 180, 45, LCD_BLUE);
    LCD_DrawCircle(70, 180, 30, LCD_BLUE);

    LCD_DrawLine(130, 140, 230, 250, LCD_MAGENTA);
    LCD_DrawLine(230, 140, 130, 250, LCD_MAGENTA);

    for (i = 0; i < 20U; i++)
    {
        LCD_DrawPoint((uint16_t)(140U + i * 4U),
                      (uint16_t)(265U + (i & 1U) * 6U), LCD_ORANGE);
    }
}

/* 文字接口：不同字号、多行、定宽数字（高位补 0） */
static void Demo_Text(void)
{
    LCD_Clear(LCD_BLACK);

    LCD_ShowString(0, 0, "size 1: ABC abc 123", LCD_WHITE, LCD_BLACK, 1);
    LCD_ShowString(0, 20, "size 2", LCD_YELLOW, LCD_BLACK, 2);
    LCD_ShowString(0, 55, "size 3", LCD_CYAN, LCD_BLACK, 3);

    LCD_ShowChar(0, 120, 'K', LCD_GREEN, LCD_BLACK, 4);

    LCD_ShowNum(0, 200, 12345, 5, LCD_RED, LCD_BLACK, 2);
    LCD_ShowNum(0, 240, 42, 5, LCD_BLUE, LCD_BLACK, 2);

    LCD_ShowString(0, 280, "multi\nline\nok", LCD_WHITE, LCD_BLACK, 1);
}

/* 图片接口：程序生成一张 64x64 渐变色，再用 LCD_ShowImage 显示 */
static void Demo_Image(void)
{
    uint16_t x;
    uint16_t y;

    LCD_Clear(LCD_WHITE);
    LCD_ShowString(0, 0, "ShowImage 64x64", LCD_BLUE, LCD_WHITE, 1);

    for (y = 0; y < 64U; y++)
    {
        for (x = 0; x < 64U; x++)
        {
            uint8_t r = (uint8_t)(x * 4U);
            uint8_t g = (uint8_t)(y * 4U);
            uint8_t b = (uint8_t)(255U - (x + y) * 2U);

            g_image[y * 64U + x] = LCD_RGB565(r, g, b);
        }
    }

    LCD_ShowImage(88, 120, 64, 64, g_image);
    LCD_DrawRect(88, 120, 64, 64, LCD_BLACK);
}

/* 读点接口：先画一块已知颜色，再读回来显示 */
static void Demo_ReadPoint(void)
{
    uint16_t color;

    LCD_Clear(LCD_WHITE);
    LCD_ShowString(0, 0, "ReadPoint", LCD_BLUE, LCD_WHITE, 1);

    LCD_Fill(80, 120, 80, 80, LCD_RED);
    LCD_DrawRect(80, 120, 80, 80, LCD_BLACK);

    color = LCD_ReadPoint(100, 140);

    LCD_ShowString(0, 40, "read:", LCD_BLACK, LCD_WHITE, 1);
    ShowHex(50, 40, color, LCD_BLACK, LCD_WHITE, 1);
    LCD_ShowString(0, 60, "expect: F800", LCD_BLACK, LCD_WHITE, 1);

    /* 注：部分批次的面板读 GRAM 不稳定，读回值可能对不上，属正常现象 */
}

/* 方向接口：依次切换 0/90/180/270 度 */
static void Demo_Direction(void)
{
    uint8_t dir;

    for (dir = LCD_DIR_0; dir <= LCD_DIR_270; dir++)
    {
        LCD_SetDir(dir);
        LCD_Clear(LCD_WHITE);

        LCD_ShowString(10, 10, "DIR", LCD_BLUE, LCD_WHITE, 2);
        LCD_ShowNum(60, 10, dir, 1, LCD_RED, LCD_WHITE, 2);

        LCD_DrawRect(0, 0, LCD_GetWidth(), LCD_GetHeight(), LCD_GREEN);

        HAL_Delay(800);
    }

    LCD_SetDir(LCD_DIR_0);
}

void App_Init(void)
{
    uint16_t id;

    /* ---------------- 初始化 ---------------- */
    id = LCD_Init();

    if (id == 0U)
    {
        /* 读不到 ID 时强制按 ILI9341 初始化（LCD_InitAs 的用法） */
        LCD_InitAs(0x9341);
        id = 0x9341;
    }

    /* ---------------- 背光 / 显示开关 ---------------- */
    LCD_BackLight(1);
    HAL_Delay(200);
    LCD_BackLight(0);
    HAL_Delay(200);
    LCD_BackLight(1);

    LCD_DisplayOff();
    HAL_Delay(200);
    LCD_DisplayOn();

    /* ---------------- 各功能页 ---------------- */
    Demo_Info(id);
    HAL_Delay(1500);

    Demo_Graphics();
    HAL_Delay(1500);

    Demo_Text();
    HAL_Delay(2000);

    Demo_Image();
    HAL_Delay(1500);

    Demo_ReadPoint();
    HAL_Delay(1500);

    LCD_Test();
    HAL_Delay(2000);

    Demo_Direction();
    HAL_Delay(500);

    LCD_Clear(LCD_WHITE);
    LCD_ShowString(10, 10, "All API demo done", LCD_GREEN, LCD_WHITE, 1);
    LCD_ShowString(10, 30, "count:", LCD_BLUE, LCD_WHITE, 1);

#if (LCD_USE_TOUCH != 0)
    LCD_TouchInit();
    LCD_Clear(LCD_WHITE);
    LCD_ShowString(0, 0, "X:", LCD_BLUE, LCD_WHITE, 2);
    LCD_ShowString(0, 300, "Y:", LCD_BLUE, LCD_WHITE, 2);
#endif
}

void App_Run(void)
{
#if (LCD_USE_TOUCH != 0)
    uint16_t x;
    uint16_t y;

    if (LCD_TouchScan(&x, &y))
    {
        uint16_t left = (x > 3U) ? (uint16_t)(x - 3U) : 0U;
        uint16_t top  = (y > 3U) ? (uint16_t)(y - 3U) : 0U;

        LCD_Fill(left, top, 7, 7, LCD_RED);

        LCD_Fill(40, 0, 200, 20, LCD_WHITE);
        LCD_ShowNum(40, 0, x, 3, LCD_RED, LCD_WHITE, 2);

        LCD_Fill(40, 300, 200, 20, LCD_WHITE);
        LCD_ShowNum(40, 300, y, 3, LCD_RED, LCD_WHITE, 2);
    }
#else
    static uint32_t tick = 0;
    static uint32_t count = 0;

    if ((HAL_GetTick() - tick) >= 500U)
    {
        tick = HAL_GetTick();
        count++;

        LCD_ShowNum(80, 30, count, 6, LCD_RED, LCD_WHITE, 1);
    }
#endif
}
