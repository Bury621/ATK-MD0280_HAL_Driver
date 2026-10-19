# ATK-MD0280 · 2.8" TFT LCD 驱动（STM32F103 + FSMC）

[正点原子 ATK-MD0280](https://www.alientek.com) 2.8 寸 320×240 TFT 模块的轻量驱动，支持 **ILI9341** 和 **ST7789V** 两种面板自动识别，可选 XPT2046 电阻触摸。

目标只有一个：**简单**。把 `lcd.c` 加进工程、`#include "lcd.h"`、调一次 `LCD_Init()` 就能用，不需要在 CubeMX 里配 FSMC，也不用改 `stm32f1xx_hal_conf.h`。

---

## 特性

- 自动识别面板控制器（`0x9341` / `0x8552`），两套初始化序列内置
- FSMC 直接写寄存器，**零 CubeMX 配置**
- 只依赖 `HAL_GPIO` + `HAL_Delay`，不依赖 `delay.c` / `sys.h`
- 引脚、片选、地址线、时序全部集中在 `lcd.h` 顶部，换板子只改一处
- 内置 8×16 ASCII 字库，支持整数倍放大
- 绘图、文字、图片接口齐全
- 可选 XPT2046 触摸：带消抖、5 点校准、参数可回填、开机免校准
- 约 6 KB Flash（不含触摸），触摸再加约 3 KB

## 硬件要求

| 项目 | 要求 |
| --- | --- |
| MCU | STM32F103xE / xG（带 FSMC，如 STM32F103ZET6） |
| 主频 | 72 MHz（FSMC 时序按 HCLK 计算） |
| 模块 | ATK-MD0280（2.8" 320×240，ILI9341 或 ST7789V） |
| 接线 | 正点原子 F103 开发板 LCD 接口，或按下表自行连接 |

## 接线

### 模块引脚 → MCU 引脚

模块是 2×20 的 2.54 mm 排针，丝印分别是 `RS`、`RD`、`WR`、`CS`、`D0`~`D15`、`BL` 等，对应关系：

| 模块引脚 | MCU 引脚 | FSMC 信号 | 说明 |
| --- | --- | --- | --- |
| CS | PG12 | FSMC_NE4 | 片选，低有效 |
| RS | PG0 | FSMC_A10 | 0 = 命令，1 = 数据 |
| RD | PD4 | FSMC_NOE | 读使能 |
| WR | PD5 | FSMC_NWE | 写使能 |
| D0 | PD14 | FSMC_D0 | 数据总线（16 位） |
| D1 | PD15 | FSMC_D1 | |
| D2 | PD0 | FSMC_D2 | |
| D3 | PD1 | FSMC_D3 | |
| D4 | PE7 | FSMC_D4 | |
| D5 | PE8 | FSMC_D5 | |
| D6 | PE9 | FSMC_D6 | |
| D7 | PE10 | FSMC_D7 | |
| D8 | PE11 | FSMC_D8 | |
| D9 | PE12 | FSMC_D9 | |
| D10 | PE13 | FSMC_D10 | |
| D11 | PE14 | FSMC_D11 | |
| D12 | PE15 | FSMC_D12 | |
| D13 | PD8 | FSMC_D13 | |
| D14 | PD9 | FSMC_D14 | |
| D15 | PD10 | FSMC_D15 | |
| BL | PB0 | — | 背光，高电平点亮 |
| T_PEN | PF10 | — | 触摸按下检测，低有效 |
| T_CS | PF11 | — | 触摸片选 |
| T_CLK | PB1 | — | 触摸 SPI 时钟 |
| T_MOSI | PF9 | — | 触摸 SPI 数据输出 |
| T_MISO | PB2 | — | 触摸 SPI 数据输入 |
| VCC / GND | 3.3V / GND | — | 逻辑电平 3.3 V |

> 数据线是 16 位并口，**D0~D15 必须全接**，只接 8 位不行。

### 为什么偏偏是这些引脚

F103 的 FSMC 引脚是**芯片内部固定映射**的，不能随便选：`FSMC_D0` 只能是 PD14、`FSMC_NOE` 只能是 PD4……所以上表不是我们挑的，是 F103ZE 的 AF 表决定的。

真正**可以选**的只有两样：

1. **用哪一路片选（NE1~NE4）** —— 决定用哪个 CS 引脚、以及基地址；
2. **RS 接哪根地址线（A0~A25）** —— 决定命令/数据地址的偏移。

其余数据/控制线照着上表接就行，换板子时把 `lcd.h` 里的引脚组改掉即可（见后面的移植指南）。

## FSMC 是怎么配的

### 片选与基地址

驱动把屏幕当成一块 **16 位 SRAM**，挂在 FSMC Bank1 的某个 NE 上：

| 片选 | 基地址 | F103ZE 上对应的 CS 引脚 |
| --- | --- | --- |
| NE1 | `0x60000000` | PD7 |
| NE2 | `0x64000000` | PG9 |
| NE3 | `0x68000000` | PG10 |
| **NE4（默认）** | **`0x6C000000`** | **PG12** |

`lcd.h` 里只要写 `#define LCD_FSMC_NE 4`，基地址会自动算出来，不用手填。

### 命令地址 / 数据地址怎么来的

RS 接在 A10 上，而总线是 16 位的，所以字节地址要左移 1 位才对上 FSMC 的地址线编号：

```
命令地址 = 基地址 + (((1 << 10) - 1) << 1) = 0x6C000000 + 0x7FE = 0x6C0007FE
数据地址 = 基地址 +  ( 1 << 10      << 1) = 0x6C000000 + 0x800 = 0x6C000800
```

也就是说，访问 `0x6C0007FE` 时 A10 = 0（RS 低，命令），访问 `0x6C000800` 时 A10 = 1（RS 高，数据）。`lcd.c` 里就是这两行：

```c
#define LCD_CMD_ADDR  (LCD_FSMC_BASE | (((1UL << LCD_FSMC_RS_BIT) - 1UL) << 1))
#define LCD_DAT_ADDR  (LCD_FSMC_BASE | ((1UL << LCD_FSMC_RS_BIT) << 1))
```

### 时序

`lcd.h` 里的四个宏，单位都是 HCLK（72 MHz 时 1 个 HCLK ≈ 13.9 ns）：

| 宏 | 默认值 | 含义 |
| --- | --- | --- |
| `LCD_READ_ADDSET` | 0 | 读：地址建立时间 |
| `LCD_READ_DATAST` | 15 | 读：数据建立时间（ID / GRAM 读很慢，给足） |
| `LCD_WRITE_ADDSET` | 0 | 写：地址建立时间 |
| `LCD_WRITE_DATAST` | 3 | 写：数据建立时间，约 70 ns |

这代面板写周期 `tWC ≥ 66 ns`，所以 72 MHz 下 `DATAST` 至少 3。屏幕花屏、偶发白屏时，先把 `LCD_WRITE_DATAST` 加到 5 试试。

寄存器层面就是这几行（`lcd_fsmc_init()`）：

```c
FSMC_Bank1->BTCR[idx]     = 0;                              /* 先关 bank */
FSMC_Bank1->BTCR[idx + 1] = read_timing;                    /* BTR */
FSMC_Bank1E->BWTR[idx]    = write_timing;                   /* BWTR */
FSMC_Bank1->BTCR[idx]     = MBKEN | MWID_0 | WREN | EXTMOD; /* 16 位 + 写使能 + 独立读写时序 */
```

`idx = (LCD_FSMC_NE - 1) * 2`。CMSIS 把 `BTCR[]` / `BWTR[]` 声明成 `uint32_t` 数组，而每个 bank 实际占 8 字节（中间夹了保留字），所以索引隔一个。

## 快速开始

### 1. 添加文件

把 `lcd.c`、`lcd.h`、`lcd_font.h` 复制到工程（例如 `Hardware/`），把 `lcd.c` 加入编译，并把该目录加进头文件搜索路径。

### 2. 初始化

`LCD_Init()` 必须在 `HAL_Init()`、`SystemClock_Config()`、`MX_GPIO_Init()` **之后**调用，因为驱动会重新配置这些引脚。

```c
#include "lcd.h"

int main(void)
{
    HAL_Init();
    SystemClock_Config();
    MX_GPIO_Init();

    if (LCD_Init() == 0)          /* 0 = 没识别到面板 */
    {
        while (1) { }
    }

    LCD_Clear(LCD_WHITE);
    LCD_ShowString(10, 10, "Hello ATK-MD0280", LCD_RED, LCD_WHITE, 2);

    while (1) { }
}
```

### 3. 跑示例（可选）

`examples/` 里的两个程序都提供 `App_Init()` / `App_Run()`：

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

- `app_2048.c` —— 2048 小游戏，上下左右滑动合并数字
- `app_draw.c` —— 触摸画板，三种进校准方式
- `app_demo.c` —— 所有显示 API 依次演示

**每次只能编译一个**，否则 `App_Init` 重复定义。

## API 速查

### 初始化与显示控制

```c
uint16_t LCD_Init(void);              // 返回 0x9341 / 0x8552，失败返回 0
void     LCD_InitAs(uint16_t id);     // 读不到 ID 时强制指定控制器
void     LCD_SetDir(uint8_t dir);     // LCD_DIR_0 / 90 / 180 / 270
uint8_t  LCD_GetDir(void);
uint16_t LCD_GetWidth(void);
uint16_t LCD_GetHeight(void);
uint16_t LCD_ReadID(void);
void     LCD_BackLight(uint8_t on);
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
void LCD_ShowString(x, y, "text", fg, bg, size);      // 支持 '\n'
void LCD_ShowNum   (x, y, value, len, fg, bg, size);  // 定宽，高位补 0
```

`size` 是放大倍数：1 → 8×16，2 → 16×32。颜色用 `LCD_RED`、`LCD_WHITE`… 或 `LCD_RGB565(r, g, b)`。

## 触摸与校准

`lcd.h` 里把 `LCD_USE_TOUCH` 设为 1（或编译选项 `-DLCD_USE_TOUCH=1`）。

开机直接使用 `LCD_TOUCH_DEF_*` 参数，不需要每次校准：

```c
#define LCD_TOUCH_DEF_X_SCALE       0.1540f
#define LCD_TOUCH_DEF_Y_SCALE       0.0776f
#define LCD_TOUCH_DEF_X_CENTER      1961U
#define LCD_TOUCH_DEF_Y_CENTER      1873U
```

> 这组值是某块面板的实测值，每块屏有差异。第一次用建议校准一次，把屏上显示的新值替换到这里。

### 接口

```c
void             LCD_TouchHwInit(void);       // 只初始化引脚，不阻塞
lcd_touch_cal_t  LCD_TouchCalibrate(void);    // 5 点校准 + 屏上输出 + 返回参数
void             LCD_TouchShowCal(void);      // 再显示一次当前参数
void             LCD_TouchInit(void);         // 上面两个的组合
uint8_t          LCD_TouchScan(uint16_t *x, uint16_t *y);
uint8_t          LCD_TouchIsDown(void);       // 只读 PEN，不依赖坐标
void             LCD_TouchGetCal(lcd_touch_cal_t *cal);
void             LCD_TouchSetCal(const lcd_touch_cal_t *cal);
```

`LCD_TouchCalibrate()` 阻塞执行：依次画 5 个十字，等点击（20 ms 消抖 + 30 ms 稳定延时），算完显示4 个参数。

示例程序里给了三种进校准的方式：上电按住屏幕、画布上按住不动 3 秒、点 CAL 按钮。

## 移植指南

### A. 同芯片，换一块板子（最常见）

只改 `lcd.h` 顶部的配置块，`lcd.c` 一行都不用动：

```c
/* 1. 片选：用 NE4 就写 4 */
#define LCD_FSMC_NE                 4U

/* 2. RS 接的是哪根地址线 */
#define LCD_FSMC_RS_BIT             10U

/* 3. 引脚组：{端口, 引脚掩码}，按你的实际接线写 */
#define LCD_FSMC_PIN_GROUPS                                                   \
    { GPIOD, GPIO_PIN_0 | GPIO_PIN_1 | ... },                                 \
    { GPIOE, GPIO_PIN_7 | ... },                                              \
    { GPIOG, GPIO_PIN_0 | GPIO_PIN_12 }

/* 4. 用到的端口时钟 */
#define LCD_GPIO_CLK_ENABLE()   do { __HAL_RCC_GPIOD_CLK_ENABLE(); ... } while (0)

/* 5. 背光脚 */
#define LCD_BL_PORT             GPIOB
#define LCD_BL_PIN              GPIO_PIN_0
```

改完记得把 `LCD_FSMC_NE` 对应的 CS 引脚也写进引脚组（NE1→PD7，NE2→PG9，NE3→PG10，NE4→PG12，具体以芯片数据手册 AF 表为准）。

### B. 换片选 / 换 RS 地址线

比如改用 NE2、RS 接 A6：

```c
#define LCD_FSMC_NE       2U      /* 基地址自动变成 0x64000000，CS 改接 PG9 */
#define LCD_FSMC_RS_BIT   6U      /* 命令/数据地址自动变成 0x6400007E / 0x64000080 */
```

同时把 `LCD_FSMC_PIN_GROUPS` 里的 CS、RS 引脚改成新的。地址计算不用管，宏会处理。

### C. 换其它 STM32F1（VE / ZE / ZG）

FSMC 外设完全一样，只改引脚：

1. 查新芯片的 FSMC 引脚 AF 表，重写 `LCD_FSMC_PIN_GROUPS`；
2. 确认 `RCC_AHBENR_FSMCEN` 存在（F103 高密度型号都有）；
3. 触摸引脚按新板子改 `LCD_TOUCH_*`；
4. 主频不是 72 MHz 时按下面的公式重算时序。

**时序换算**（单位：HCLK 周期）：

```
写周期 ≈ (LCD_WRITE_DATAST + LCD_WRITE_ADDSET + 2) × (1 / HCLK)
要求 ≥ 66 ns（面板 tWC）
```

例如 48 MHz 时 HCLK ≈ 20.8 ns，`DATAST = 3` 约 104 ns，够用；168 MHz 时 HCLK ≈ 5.95 ns，`DATAST` 要 10 才够。**主频越高，DATAST 越大**。

### D. 换到 STM32F4 / F7（FSMC 改名 FMC）

寄存器布局基本一致，但要改这几处：

| F1 | F4 / F7 |
| --- | --- |
| `#include "stm32f1xx_hal.h"` | `#include "stm32f4xx_hal.h"` |
| `RCC->AHBENR \|= RCC_AHBENR_FSMCEN` | `__HAL_RCC_FMC_CLK_ENABLE()`（AHB3） |
| `FSMC_Bank1` / `FSMC_Bank1E` | `FMC_Bank1` / `FMC_Bank1E` |
| GPIO 只需 `GPIO_MODE_AF_PP` | 还要填 `gpio_init_struct.Alternate = GPIO_AF12_FMC;` |

`BTCR[]` / `BWTR[]` 的结构和索引算法不变，`lcd_set_window()`、初始化序列、绘图、触摸全部不用改；只要在 `lcd_hw_init()` 里补上 `Alternate` 字段。

### E. 没有 FSMC 的芯片（F103C8、G0、L4…）

这个驱动**不能直接用**，因为它靠 FSMC 硬件产生 8080 时序。两条路：

1. **GPIO 模拟 8080 并口**（能跑，但慢）：删掉 `lcd_fsmc_init()`，把三个底层函数换成 GPIO 版：

   ```c
   static void lcd_write_cmd(uint16_t cmd) { LCD_RS(0); lcd_bus_write(cmd); }
   static void lcd_write_data(uint16_t dat) { LCD_RS(1); lcd_bus_write(dat); }

   static void lcd_bus_write(uint16_t dat)
   {
       LCD_DATA_PORT->ODR = dat;     /* D0~D15 在同一端口时 */
       LCD_WR(0); __NOP(); __NOP();
       LCD_WR(1);
   }
   ```

   其余（初始化序列、窗口、绘图、触摸）都不用动。GPIO 翻转速度有限，全屏刷新会明显变慢。

2. **换 SPI 屏**：本驱动是并口版，SPI 型号（ST7735/ST7789 SPI 等）需要另一套驱动。

### 移植检查清单

- [ ] `LCD_FSMC_NE` 和 CS 引脚对应
- [ ] `LCD_FSMC_RS_BIT` 和 RS 引脚对应
- [ ] `LCD_FSMC_PIN_GROUPS` 覆盖全部数据/控制脚
- [ ] `LCD_GPIO_CLK_ENABLE()` 打开了所有用到的端口
- [ ] 时序按主频重算，写周期满足 ≥ 66 ns
- [ ] 触摸引脚与 `LCD_TOUCH_*` 一致（不用触摸可关 `LCD_USE_TOUCH`）
- [ ] `LCD_Init()` 在时钟和 GPIO 初始化之后调用

## 注意事项

- **读点**：这批面板读 GRAM 普遍不稳定，`LCD_ReadPoint()` 仅供参考，别拿来做逻辑判断。
- **字符集**：只支持 ASCII，中文需要另加 GB2312 字库。
- **RTOS**：驱动没有加锁，多任务刷屏请自行加互斥量。
- **引脚冲突**：触摸占 PB1/PB2/PF9/PF10/PF11，背光占 PB0。

## 目录结构

```
ATK-MD0280/
├── lcd.c                 驱动实现
├── lcd.h                 接口、引脚/FSMC 配置、颜色、校准默认参数
├── lcd_font.h            8×16 ASCII 字库
├── examples/
│   ├── app_2048.c        2048 小游戏
│   ├── app_draw.c        触摸画板
│   ├── app_demo.c        全 API 演示
│   ├── app.h
│   └── README.md
├── LICENSE               MIT
└── README.md
```

## English summary

A minimal driver for the ALIENTEK ATK-MD0280 2.8" 320×240 TFT module on
STM32F103 (FSMC). It auto-detects ILI9341 / ST7789V panels, configures the
FSMC by writing its registers directly (no CubeMX setup, no HAL SRAM module),
and depends only on `HAL_GPIO` and `HAL_Delay`.

All board-specific settings (chip-select, RS address line, pin groups, timing)
live at the top of `lcd.h`, so porting to another board is a single-file edit.

## 许可

本项目采用 [MIT License](LICENSE)。

初始化序列、8×16 点阵字库参考自正点原子 ATK-MD0280 官方示例，相关内容版权归
广州市星翼电子科技有限公司（ALIENTEK）所有；本项目是在其公开示例基础上的精简重写，
商用前请自行确认相关授权。
