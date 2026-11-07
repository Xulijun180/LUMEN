# Lumen

## 简介

Lumen 是一个用 C 语言写的 128×64 单色 OLED 界面框架，适合带小屏幕和几个按键的嵌入式设备。它管理页面栈和弹窗，所有动画都用弹簧（二阶系统）或贝塞尔曲线驱动，运行时不调用 malloc。框架本身不包含任何硬件代码：屏幕和按键由移植层通过 `lm_port_t` 提供，[u8g2](https://github.com/olikraus/u8g2) 只用来画文字。

- 🌈 **界面**
    - 页面栈：push / pop / replace / 回主页
    - 页面切换动画：滑动、缩放、淡入淡出，动画中途可以反向
    - 弹窗：提示条（toast）、确认对话框（dialog）、底部数值调节（sheet）
    - 列表菜单：用 const 表格声明，表格数据放在 Flash 里
- 🔔 **特点**
    - 弹簧动画，可以随时打断并改变目标
    - 抗锯齿矢量绘图：圆角矩形、圆弧、粗线、多边形，坐标用 float
    - 只发送有变化的页：按页计算哈希，没变的页不发
    - 设置项带观察者，值改了自动通知（亮度、反色、保存等）
    - 支持 4 键和 3 键两种按键布局，页面代码不用改
    - 自动变暗、自动息屏，息屏和唤醒也有动画
- 📆 **控件**
    - 开关、选项循环、数值调节、子菜单、动作项
    - 滚动数字（像机械计数器一样逐位滚动）
    - 跑马灯文字、滚动条、加载圈、电池、Wi-Fi 图标
    - 矢量图标，在菜单里被选中时会动
- ⚙️ **资源占用**（STM32C532 例程，GCC，库按 -O2 编译）

    | 部分 | Flash | RAM |
    | ---- | ----- | --- |
    | 库（core + widget + fonts + u8g2） | 38 KB | 4.4 KB（含 `lm_ui_t` 3.9 KB） |
    | 例程演示页面（apps） | 17 KB | 5.6 KB |

    `lm_ui_t` 里有 3 块 1 KB 帧缓冲：一块是屏幕，两块给切换动画用。

## 项目结构

```plaintext
Lumen
├─core                  # 框架核心
│    ├─lm_ui.c/h        # 页面栈、弹窗、切换动画、电源，以及移植接口 lm_port_t
│    ├─lm_gfx.c/h       # 帧缓冲和抗锯齿绘图
│    ├─lm_anim.c/h      # 弹簧、缓动、贝塞尔曲线
│    ├─lm_input.c/h     # 按键消抖、长按、连发、事件队列
│    ├─lm_settings.c/h  # 设置项和观察者
│    ├─lm_overlay.c/h   # toast / dialog / sheet
│    ├─lm_math.c/h
│    └─lm_config.h      # 编译期配置
├─widget                # 控件
│    ├─lm_menu.c/h      # 声明式列表菜单
│    ├─lm_widgets.c/h   # 滚动数字、跑马灯、开关等
│    └─lm_icons.c/h     # 矢量图标
├─fonts                 # 字体
│    ├─lm_fonts.h       # 字体角色（UI / TINY / MID / BIG / LOGO）
│    └─u8g2_fonts_subset.c
├─third_party
│    └─u8g2             # u8g2 源码
├─examples              # 示例工程
│    └─STM32C532CCT6
│         ├─apps        # 演示页面：开机、主页、时钟、音乐、监控、设置、关于
│         ├─port        # 移植层：I2C + DMA 刷屏、按键
│         └─...         # CubeMX2 工程
├─CMakeLists.txt        # 库 lumen_ui
└─README.md
```

## 如何移植

### CMake

用 `add_subdirectory` 把仓库根目录加进来，再链接 `lumen_ui`：

```cmake
add_subdirectory(path/to/Lumen lumen)
target_link_libraries(your_app lumen_ui)
```

库会用变量 `CPU_FLAGS` 作为 CPU 编译选项（例如 `-mcpu=cortex-m33 -mfpu=fpv5-sp-d16 -mfloat-abi=hard`），所以要在 `add_subdirectory` 之前设置它。优化等级由缓存变量 `LUMEN_OPT` 控制，默认是 `-O2`。

### Keil / 其他 IDE

把 `core`、`widget`、`fonts` 里的所有 .c 文件和 `third_party/u8g2/csrc` 里的所有 .c 文件加入工程，再把 `core`、`widget`、`fonts`、`third_party/u8g2/csrc` 加到 include 路径。编译需要 C99 或更高的标准，并且要链接 libm。

## 如何使用

1. **实现移植接口。** 填一个 `lm_port_t`，除了下面注明的几项，其余接口都可以留 NULL：

    ```c
    static uint32_t port_millis(void *ctx) { return HAL_GetTick(); }

    static uint8_t port_keys(void *ctx)          /* 返回按下的键，LM_KEYBIT 掩码 */
    {
        uint8_t m = 0;
        if (key_up())    m |= LM_KEYBIT(LM_KEY_UP);
        if (key_down())  m |= LM_KEYBIT(LM_KEY_DOWN);
        if (key_enter()) m |= LM_KEYBIT(LM_KEY_ENTER);
        return m;
    }

    /* fb 是 1024 字节的页格式帧缓冲（和 SSD1306 一样），pages 的第 p 位为 1 表示第 p 页有变化 */
    static void port_flush(void *ctx, const uint8_t *fb, uint8_t pages)
    {
        for (uint8_t p = 0; p < LM_PAGES; p++)
            if (pages & (1u << p))
                oled_write_page(p, fb + p * LM_W);   /* 发 128 字节到屏幕第 p 页 */
    }

    static void port_contrast(void *ctx, uint8_t level) { oled_set_contrast(level); }
    static void port_power(void *ctx, bool on)          { oled_display_on(on); }

    lm_port_t port = {0};
    port.millis       = port_millis;     /* 必需 */
    port.read_keys    = port_keys;       /* 必需，除非在中断里用 lm_ui_post() 推送按键事件 */
    port.flush        = port_flush;      /* 必需 */
    port.set_contrast = port_contrast;   /* 可选，亮度和自动变暗要用 */
    port.set_power    = port_power;      /* 可选，息屏要用 */
    port.key_layout   = LM_LAYOUT_3KEY;  /* 默认是 4 键：PREV / NEXT / OK / BACK */
    ```

    可选接口还有 `haptic`（振动）、`rtc_get`（时间）、`settings_load` 和 `settings_save`（保存设置）。屏幕驱动用什么由你决定，可以是 u8x8、自己写的驱动，也可以用 DMA。`flush` 可以在传输结束前就返回，但返回前要把数据拷走，因为下一帧会覆盖 `fb`。例程里的 [lumen_port.c](examples/STM32C532CCT6/port/lumen_port.c) 就是用 DMA 异步刷屏的。

2. **写页面。** 菜单直接用表格声明：

    ```c
    #include "lm_menu.h"
    #include "lm_overlay.h"

    static const lm_option_t SLEEP_OPTS[] = { {15, "15s"}, {30, "30s"}, {0, "Never"} };

    static void open_reset(lm_menu_t *m, int i)
    {
        lm_toast(m->base.ui, "Settings reset", LM_ICON_CHECK);
    }

    static const lm_menu_item_t ITEMS[] = {
        LM_ITEM_TOGGLE("Invert", LM_S_INVERT),                 /* 绑定设置项，改了自动生效 */
        LM_ITEM_OPTION("Auto sleep", LM_S_AUTOSLEEP, SLEEP_OPTS),
        LM_ITEM_ACTION("Reset", open_reset),
    };
    static LM_MENU_DEFINE(s_menu, "Settings", ITEMS);
    ```

    自定义页面实现 `lm_page_ops_t` 里需要的回调，比如 `enter`、`key`、`update`、`render`，不需要的回调留 0，然后在 `render` 里用 `lm_text`、`lm_rbox`、`lm_arc` 等函数绘图。完整例子可以看例程的 [apps](examples/STM32C532CCT6/apps) 目录。

3. **启动并运行主循环：**

    ```c
    static lm_ui_t ui;
    /* 设置项默认值，顺序和 LM_S_* 一致：亮度 %、反色、自动息屏 s、动画速度 %、振动 */
    static const int32_t defaults[] = { 80, 0, 30, 100, 1 };

    lm_ui_init(&ui, &port, defaults, 5);
    lm_ui_start(&ui, &s_menu.base);     /* 根页面 */
    for (;;) {
        lm_ui_tick(&ui);                /* 读按键、更新动画、绘制、刷屏 */
    }
    ```

    `lm_ui_idle()` 返回 true 时，表示当前没有动画在跑，可以让 MCU 进入休眠。

4. **按需修改配置。** 页面栈深度、弹窗数量、菜单项上限、按键时间等参数都在 [lm_config.h](core/lm_config.h) 里。要覆盖这些参数，定义宏 `LM_USER_CONFIG`，并提供 `lm_user_config.h`。字体也可以换，在 [lm_fonts.h](fonts/lm_fonts.h) 里覆盖 `LM_FONT_*` 宏即可。

## 按键

| 4 键布局（默认） | 3 键布局 |
| --- | --- |
| PREV / NEXT：上一项 / 下一项，按住连发 | UP / DOWN：同左 |
| OK：确认，长按为 OK LONG | ENTER 短按：确认 |
| BACK：返回，长按回主页（在主页时长按为息屏） | ENTER 按住 0.4 s 后松开：返回<br>ENTER 按住 1.2 s：回主页（在主页时为息屏） |

两种布局下页面收到的都是同样的 4 个逻辑键，所以页面代码不用为某种布局单独写。

## 示例程序

- **STM32C532CCT6**
    - 开发环境：STM32CubeMX2 + CMake + Ninja（arm-none-eabi-gcc 14），CLion 可以直接打开 `examples/STM32C532CCT6`
    - 屏幕：0.96/1.3 寸 OLED 128×64，I2C 接口，地址 0x3C 和 0x3D 自动检测，驱动用 u8x8 的 SH1106
    - I2C：硬件 I2C1（SCL PB8，SDA PB9），1 MHz，用 LPDMA1 异步刷屏，传输和下一帧的绘制同时进行
    - 按键：3 键，低电平有效，用内部上拉
        - 上：PB7
        - 下：PB6
        - 确认：PB5
    - 调试串口：USART2 TX PA2，115200 8N1，输出启动过程和帧率日志
    - 编译：

        ```bash
        cd examples/STM32C532CCT6
        cmake --preset debug_GCC_STM32C532CCT6
        cmake --build build/debug_GCC_STM32C532CCT6
        ```

        工程要求 CMake 3.30 以上。STM32CubeCLT 1.21 自带的是 3.28，版本不够，可以改用 CLion 自带的或单独安装的 CMake。

    ### 注意：SSD1306 规格书写的 I2C 最高速率是 400 kHz，1 MHz 超出了规格。如果屏幕显示不稳定，把 `lumen_port.c` 里的 `LUMEN_I2C_HZ` 改小。

## 后期规划

- [ ] 支持更多分辨率（目前固定为 128×64）
- [ ] 在 PC 上编译并跑单元测试和模拟器
- [ ] 设置项保存到 Flash
- [ ] 空闲时进入低功耗

## 问题和建议

有问题或建议，欢迎提交 [issue](https://github.com/Xulijun180/LUMEN/issues) 讨论。

## 鸣谢

- u8g2 库：https://github.com/olikraus/u8g2
- 目录结构参考：[MiaoUI](https://github.com/JFeng-Z/MiaoUI)
