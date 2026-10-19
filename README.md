# ATK-MD0280 · 2.8" TFT LCD 驱动（STM32F103 + FSMC）

[正点原子 ATK-MD0280](https://www.alientek.com) 2.8 寸 320×240 TFT 模块的轻量驱动，支持 **ILI9341** 和 **ST7789V** 两种面板自动识别，可选 XPT2046 电阻触摸。

写这个驱动的目标只有一个：**简单**。把 `lcd.c` 加进工程、`#include "lcd.h"`，调一下 `LCD_Init()` 就能用，不需要在 CubeMX 里配 FSMC，也不用改 `stm32f1xx_hal_conf.h`。

---

## 特性

- 自动识别面板控制器（`0x9341` / `0x8552`），两套初始化序列都已内置
- FSMC 直接配置寄存器，**零 CubeMX 配置**
- 只依赖 `HAL_GPIO` + `HAL_Delay`（CubeMX 工程本来就有），不依赖任何 `delay.c` / `sys.h`
- 内置 8×16 ASCII 字库，支持整数倍放大（1/2/3…）
- 绘图接口：清屏、填充、画点、读点、画线、矩形、圆、图片
- 文字接口：字符、字符串（自动换行）、定宽数字
- 触摸（可选）：XPT2046 位操作 SPI，**带消抖**、5 点校准、参数可回填、开机免校准
- 约 6 KB Flash（不含触摸），触摸再加约 3 KB

## 硬件要求

| 项目 | 要求 |
| --- | --- |
| MCU | STM32F103xE / xG（带 FSMC，如 STM32F103ZET6） |
| 主频 | 72 MHz（HCLK = 72 MHz，FSMC 时序按此计算） |
| 模块 | ATK-MD0280（2.8" 320×240，ILI9341 或 ST7789V） |
| 接线 | 正点原子 F103 开发板 LCD 接口，或按下表自行连接 |

### 接线表

| 信号 | 引脚 | 说明 |
| --- | --- | --- |
| CS | PG12 | FSMC_NE4 |
| RS | PG0 | FSMC_A10 |
| RD | PD4 | FSMC_NOE |
| WR | PD5 | FSMC_NWE |
| D0 / D1 | PD14 / PD15 | 16 位数据总线 |
| D2 / D3 | PD0 / PD1 | |
| D4 ~ D12 | PE7 ~ PE15 | |
| D13 ~ D15 | PD8 / PD9 / PD10 | |
| BL | PB0 | 背光，高电平点亮 |
| PEN | PF10 | 触摸按下检测（低有效） |
| TCS | PF11 | 触摸片选 |
| CLK | PB1 | 触摸 SPI 时钟 |
| MOSI | PF9 | 触摸 SPI 数据输出 |
| MISO | PB2 | 触摸 SPI 数据输入 |

> 换板子时只需要改 `lcd.h` 顶部的 `LCD_BL_PIN`、`LCD_FSMC_BASE`、`LCD_FSMC_RS_BIT`，以及触摸那一组引脚宏。

## 快速开始

### 1. 添加文件

把 `lcd.c`、`lcd.h`、`lcd_font.h` 复制到工程里（例如 `Hardware/` 目录），然后把 `lcd.c` 加入编译，并把该目录加进头文件搜索路径。

### 2. 初始化

`LCD_Init()` 必须在 `HAL_Init()`、`SystemClock_Config()`、`MX_GPIO_Init()` **之后**调用，因为驱动会重新配置这些引脚。

```c
#include "lcd.h"

int main(void)
{
    HAL_Init();
    SystemClock_Config();
    MX_GPIO_Init();

    if (LCD_Init() == 0)      /* 返回 0 表示没识别到面板 */
    {
        while (1) { }
    }

    LCD_Clear(LCD_WHITE);
    LCD_ShowString(10, 10, "Hello ATK-MD0280", LCD_RED, LCD_WHITE, 2);

    while (1)
    {
        /* ... */
    }
}
```

### 3. 跑示例（可选）

`examples/` 里有两个现成的应用层程序，二者都提供 `App_Init()` / `App_Run()`，在 `main` 里这样接：

```c
#include "app.h"

/* USER CODE BEGIN 2 */
App_Init();
/* USER CODE END 2 */

while (1)
{
    App_Run();
}
```

- `app_draw.c` —— 触摸画板：上电直接画，点 CAL / 按住 3 秒 / 上电按住可重新校准
- `app_demo.c` —— 把所有显示 API 依次演示一遍（图形、文字、图片、方向、自检）

**两个文件只能选一个编译**，否则 `App_Init` 会重复定义。

## API 速查

### 初始化与显示控制

```c
uint16_t LCD_Init(void);              // 初始化，返回 0x9341 / 0x8552，失败返回 0
void     LCD_InitAs(uint16_t id);     // 读不到 ID 时强制按指定控制器初始化
void     LCD_SetDir(uint8_t dir);     // LCD_DIR_0 / 90 / 180 / 270
uint8_t  LCD_GetDir(void);
uint16_t LCD_GetWidth(void);          // 当前方向下的宽
uint16_t LCD_GetHeight(void);         // 当前方向下的高
uint16_t LCD_ReadID(void);
void     LCD_BackLight(uint8_t on);   // 背光
void     LCD_DisplayOn(void);
void     LCD_DisplayOff(void);
void     LCD_Test(void);              // 彩条 + ID 自检
```

### 绘图

```c
void     LCD_Clear(uint16_t color);
void     LCD_Fill(uint16_t x, uint16_t y, uint16_t w, uint16_t h, uint16_t color);
void     LCD_DrawPoint(uint16_t x, uint16_t y, uint16_t color);
uint16_t LCD_ReadPoint(uint16_t x, uint16_t y);      // 部分面板读 GRAM 不稳定
void     LCD_DrawLine(uint16_t x1, uint16_t y1, uint16_t x2, uint16_t y2, uint16_t color);
void     LCD_DrawRect(uint16_t x, uint16_t y, uint16_t w, uint16_t h, uint16_t color);
void     LCD_DrawCircle(uint16_t x, uint16_t y, uint16_t r, uint16_t color);
void     LCD_ShowImage(uint16_t x, uint16_t y, uint16_t w, uint16_t h, const uint16_t *img);
```

### 文字

```c
void LCD_ShowChar  (x, y, ch, fg, bg, size);
void LCD_ShowString(x, y, "text", fg, bg, size);   // 支持 '\n' 换行
void LCD_ShowNum   (x, y, value, len, fg, bg, size);  // 定宽，高位补 0
```

`size` 是放大倍数：1 → 8×16，2 → 16×32，依此类推。字符格会先用 `bg` 填满，所以能覆盖旧内容。

颜色使用 `LCD_RED`、`LCD_WHITE`… 或 `LCD_RGB565(r, g, b)`。

## 触摸与校准

### 打开触摸

`lcd.h` 里把 `LCD_USE_TOUCH` 设为 1（也可以在编译选项里加 `-DLCD_USE_TOUCH=1`）：

```c
#define LCD_USE_TOUCH   1
```

### 开机免校准

`lcd.h` 里有一组默认参数，开机直接生效，不需要每次校准：

```c
#define LCD_TOUCH_DEF_X_SCALE       0.1540f
#define LCD_TOUCH_DEF_Y_SCALE       0.0776f
#define LCD_TOUCH_DEF_X_CENTER      1961U
#define LCD_TOUCH_DEF_Y_CENTER      1873U
```

> 这组值是某块面板的实测值，**每块屏会有差异**。第一次使用建议校准一次，然后把屏上显示的新值替换到这里。

### 操作接口

```c
void             LCD_TouchHwInit(void);       // 只初始化触摸引脚，不阻塞
lcd_touch_cal_t  LCD_TouchCalibrate(void);    // 5 点校准 + 屏上输出参数 + 返回参数
void             LCD_TouchShowCal(void);      // 再次显示当前参数
void             LCD_TouchInit(void);         // 上面两个的组合
uint8_t          LCD_TouchScan(uint16_t *x, uint16_t *y);   // 1 = 有触摸
uint8_t          LCD_TouchIsDown(void);       // 只读 PEN 脚，不依赖坐标
void             LCD_TouchGetCal(lcd_touch_cal_t *cal);
void             LCD_TouchSetCal(const lcd_touch_cal_t *cal);
```

`LCD_TouchCalibrate()` 是阻塞函数：依次在 5 个位置画十字，等用户点击（带 20 ms 消抖、30 ms 稳定延时），算完把 4 个参数显示在屏上 8 秒并返回。失败时保留原参数。

校准结果抄回 `LCD_TOUCH_DEF_*` 之后，以后开机就不用再校准了。如果把参数存进 Flash/EEPROM，开机动一次 `LCD_TouchSetCal()` 也可以。

### 进校准的三种方式（示例程序里已实现）

1. **上电/复位时按住屏幕** —— 最可靠，不依赖坐标是否正确
2. **在画布上按住不动 3 秒** —— 手指移动超过 8 px 就不算
3. **点右上角 CAL 按钮** —— 坐标准确时最方便

## 注意事项

- **主频**：FSMC 读/写时序的单位是 HCLK。默认值按 72 MHz 写的，改主频后请相应调整 `LCD_READ_DATAST` / `LCD_WRITE_DATAST`。
- **读点**：这批 2.8" 面板读 GRAM 普遍不稳定，`LCD_ReadPoint()` 返回值仅供参考，别拿来做逻辑判断。
- **字符集**：只支持 ASCII。要显示中文需要另加 GB2312 点阵字库。
- **RTOS**：驱动本身没有加锁，多个任务同时刷屏需要自行加互斥量。
- **引脚冲突**：触摸占用 PB1/PB2/PF9/PF10/PF11，背光占用 PB0，注意不要和工程里其它外设冲突。

## 目录结构

```
ATK-MD0280/
├── lcd.c                 驱动实现
├── lcd.h                 接口、配置、颜色、校准默认参数
├── lcd_font.h            8×16 ASCII 字库
├── examples/
│   ├── app_draw.c        触摸画板示例
│   ├── app_demo.c        全 API 演示
│   ├── app.h             示例的公共声明
│   └── README.md         示例说明
├── LICENSE               MIT
└── README.md
```

## English summary

A minimal driver for the ALIENTEK ATK-MD0280 2.8" 320×240 TFT module on
STM32F103 (FSMC). It auto-detects ILI9341 / ST7789V panels, configures the
FSMC by writing its registers directly (no CubeMX setup, no HAL SRAM module),
and depends only on `HAL_GPIO` and `HAL_Delay`.

```c
#include "lcd.h"

LCD_Init();
LCD_Clear(LCD_WHITE);
LCD_ShowString(10, 10, "Hello", LCD_RED, LCD_WHITE, 2);
```

Optional XPT2046 resistive touch with debouncing and a 5-point calibration
that prints the resulting parameters on screen.

## 许可

本项目采用 [MIT License](LICENSE)。

初始化序列、8×16 点阵字库参考自正点原子 ATK-MD0280 官方示例，相关内容的版权归
广州市星翼电子科技有限公司（ALIENTEK）所有；本项目是在其公开示例基础上的精简重写，
如用于商业产品请自行确认相关授权。
