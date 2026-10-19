# 示例

三个示例都提供 `App_Init()` / `App_Run()`，在 `main.c` 里这样接：

```c
#include "app.h"

int main(void)
{
    HAL_Init();
    SystemClock_Config();
    MX_GPIO_Init();

    App_Init();

    while (1)
    {
        App_Run();
    }
}
```

| 文件 | 内容 |
| --- | --- |
| `app_2048.c` | **2048 小游戏**。上下左右滑动合并数字，目标 2048；游戏结束轻点重开，按住 3 秒重新校准 |
| `app_draw.c` | 触摸画板。点 CAL / 按住不动 3 秒 / 上电时按住屏幕，三种方式进 5 点校准 |
| `app_demo.c` | 把显示 API 依次演示：信息页、图形、文字、图片、读点、自检彩条、四个方向 |

**每次只能编译一个**，否则 `App_Init` 会重复定义。

`app_2048.c` 和 `app_draw.c` 需要 `lcd.h` 里 `LCD_USE_TOUCH` 为 1；`app_demo.c` 在触摸关闭时也能正常跑（只演示显示部分）。
