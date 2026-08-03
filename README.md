# STM32F407 智能人机交互系统

基于 STM32F407ZGT6 + FreeRTOS 的嵌入式人机交互系统，集成 LCD 显示、4×4 矩阵键盘、旋转编码器、SD 卡文件管理、DAC 音乐播放、RTC 实时时钟等功能，提供登录锁屏、桌面应用、系统监控等完整交互体验。

## 目录结构

```
microcomputer/
├── Core/                       # STM32CubeMX 生成的核心代码
│   ├── Inc/                    # 头文件（main.h, gpio.h, spi.h, tim.h, usart.h 等）
│   └── Src/                    # 源文件（main.c, gpio.c, spi.c, tim.c, usart.c 等）
│
├── Drivers/                    # ST 官方驱动库
│   ├── CMSIS/                  # ARM CMSIS 核心库
│   └── STM32F4xx_HAL_Driver/   # STM32F4 HAL 驱动
│
├── FreeRTOS/                   # FreeRTOS 实时操作系统
│   ├── include/                # FreeRTOS 头文件
│   ├── portable/               # 移植层（ARM_CM4F + heap_4）
│   └── source/                 # FreeRTOS 源码
│
├── User/                       # 用户应用代码（项目核心）
│   ├── app/                    # 应用程序层
│   │   ├── app_manager.c       # 应用管理器（应用切换、桌面跳转）
│   │   ├── app_registry.c      # 应用注册表（注册所有应用到桌面）
│   │   ├── desktop.c           # 桌面界面（应用图标选择）
│   │   ├── login.c             # 登录/锁屏界面（密码输入、错误锁定）
│   │   ├── monitor.c           # 系统监控任务（状态机、熄屏/唤醒、输入分发）
│   │   ├── settings.c          # 设置持久化（音量、屏幕超时，Flash Sector 11）
│   │   ├── gui.c               # GUI 工具库（字符串、按钮、矩形绘制）
│   │   ├── log_store.c         # 日志存储（环形缓冲，互斥锁保护）
│   │   ├── input_test.c        # 输入测试
│   │   ├── app_draw.c          # 绘图应用（编码器选色、按键清屏）
│   │   ├── app_file.c          # 文件管理应用（SD 卡文件增删改查，异步 I/O）
│   │   ├── file_sys.c          # 文件系统（SD 卡 Block 0 文件表管理）
│   │   ├── file_task.c         # 文件后台任务（队列驱动，避免输入阻塞）
│   │   ├── app_music.c         # 音乐播放应用（曲目选择、进度显示）
│   │   ├── music.c             # 音乐播放器（DAC 方波合成、音量控制）
│   │   ├── music_task.c        # 音乐后台任务（队列驱动，独立推进播放）
│   │   ├── app_settime.c       # 时间设置应用
│   │   ├── app_settings.c      # 设置应用（音量、屏幕超时调整）
│   │   ├── app_sysmonitor.c    # 系统监控应用（堆/栈/队列水位、错误计数）
│   │   └── app_log.c           # 日志查看应用
│   │
│   └── dev/                    # 设备驱动层
│       ├── lcd.c               # LCD 驱动（ILI9341，480×320，SPI）
│       ├── key.c               # 4×4 矩阵键盘扫描（PE0-PE3 行，PE4/PE5/PE7/PE8 列）
│       ├── encoder.c           # 旋转编码器驱动（TIM4 正交解码，PB6/PB7，SW=PB8）
│       ├── dac.c               # DAC 驱动（PA4，TIM6 定时中断方波合成）
│       ├── rtc_time.c          # RTC 实时时钟（LSE 32.768kHz，VBAT 备份）
│       └── sd_test.c           # SD 卡驱动（SPI 模式）
│
├── MDK-ARM/                    # Keil MDK-ARM 工程
│   ├── microcomputer.uvprojx   # Keil 工程文件（用 Keil 打开此文件）
│   └── startup_stm32f407xx.s  # 启动文件
│
├── microcomputer.ioc           # STM32CubeMX 工程文件（用 CubeMX 打开可重新生成外设配置）
└── .mxproject                  # CubeMX 项目标记
```

## 硬件平台

| 组件 | 型号/规格 | 引脚 |
|------|-----------|------|
| MCU | STM32F407ZGT6 (Cortex-M4, 168MHz, 1MB Flash, 192KB RAM) | - |
| LCD | ILI9341 2.8" 320×240 (横屏 480×320 使用) | SPI1 |
| 矩阵键盘 | 4×4 矩阵 | PE0-PE3 (行), PE4/PE5/PE7/PE8 (列) |
| 旋转编码器 | 带按键的旋转编码器 | PB6 (A相/TIM4_CH1), PB7 (B相/TIM4_CH2), PB8 (SW) |
| 输入模块检测 | ID 脚 | PE6 (低=接入, 高=断开) |
| SD 卡 | SPI 模式 | SPI2 |
| DAC 输出 | 内置 DAC1 | PA4 |
| RTC 晶振 | 32.768kHz LSE | PC14/PC15 |
| VBAT | 纽扣电池 (CR1220/CR2032) | VBAT 引脚 |
| 串口日志 | USART3 | PD8 (TX), PD9 (RX) |
| LED 指示 | 状态 LED | PF9, PF10 |

## 功能模块

### 系统框架
- **FreeRTOS** 实时操作系统（堆 48KB，7 级优先级，栈溢出检测）
- **状态机**：BOOT → LOGIN → DESKTOP → APP，支持熄屏/锁屏/唤醒
- **应用管理器**：统一的 `on_create/on_start/on_run/on_pause` 应用生命周期
- **日志系统**：USART3 串口输出 + SD 卡持久化（115200 波特率，互斥锁保护）

### 应用列表（桌面图标）
| 应用 | 功能 |
|------|------|
| **DRAW** | 绘图画板，编码器选色、按键清屏 |
| **MUSIC** | 音乐播放器，DAC 方波合成，音量可调且掉电保存 |
| **FILE** | SD 卡文件管理（创建/查看/编辑/删除/重命名，8.3 文件名，512 字节/文件） |
| **SETTIME** | 时间设置（时/分/秒） |
| **SETTINGS** | 系统设置（音量、屏幕超时） |
| **SYSMON** | 系统监控（堆/栈水位、队列水位、错误计数） |
| **LOG** | 日志查看 |

### 安全特性
- 登录密码（默认 1234），连续错误 3 次锁定 30 秒
- 密码提示（错误 1 次后显示首字符）
- 主动熄屏（BACK 长按）+ 被动熄屏（30 秒无操作）
- 唤醒后自动锁屏

## 如何编译

### 方法：使用 Keil MDK-ARM

1. **安装工具链**：Keil MDK-ARM v5.x + ARM Compiler v6 (ARMCLANG)
2. **安装 STM32F4 包**：在 Keil Pack Installer 中安装 `Keil.STM32F4xx_DFP`
3. **打开工程**：双击 `MDK-ARM/microcomputer.uvprojx`
4. **编译**：按 `F7` 或点击 Build 按钮
5. **下载**：连接 ST-Link/J-Link，按 `F8` 烧录

> 项目使用 ARMCLANG (AC6) 编译器。FreeRTOS 移植层为 `ARM_CM4F`（支持 Cortex-M4 FPU）。

## 如何使用

### 首次上电
1. 烧录程序后复位，进入 BOOT 状态
2. 系统初始化 LCD/RTC/SD 卡/FreeRTOS
3. 进入 LOGIN 界面，输入密码 `1234` 解锁

### 桌面操作
- **方向键**：移动光标选择应用图标
- **OK**：进入选中的应用
- **BACK**：主动熄屏（在桌面）；在应用中返回桌面
- **编码器旋转**：在 DRAW 中切换颜色；在 MUSIC 中调节音量
- **编码器按压**：在 DRAW 中清屏；在 MUSIC 中停止播放
- **30 秒无操作**：自动熄屏

### 串口日志
连接 USART3（PD8/PD9），波特率 115200，可查看：
- 系统启动信息 `[BOOT]`
- RTC 初始化状态 `[RTC]`
- 输入设备状态 `[INPUT]`
- 系统监控数据 `[MON]`
- 熄屏/唤醒事件 `[SCREEN]`

## 关键技术

### FreeRTOS 多任务架构

系统拆分为 5 个任务，按"输入最优先、I/O 次之、监控最低"原则分配优先级，避免后台操作阻塞输入响应与界面刷新。

| 任务 | 优先级 | 栈大小(字) | 周期 | 职责 |
|------|--------|-----------|------|------|
| **InputTask** | 3（最高） | 512 | 20ms | 矩阵键盘+编码器扫描、状态机（LOGIN/RUNTIME/SCREEN_OFF/LOCKED）、熄屏唤醒、应用分发、UI 重绘 |
| **FileTask** | 2 | 1024 | 事件驱动 | SD 卡块读写、文件增删改查，通过请求/响应队列与 InputTask 解耦 |
| **MusicTask** | 2 | 512 | 20ms | 音符推进、DAC 频率切换，通过命令队列接收 app_music 指令 |
| **LedTask** | 2 | 256 | 500ms | LED 翻转，肉眼确认调度器运行 |
| **MonitorTask** | 1（最低） | 1024 | 1000ms | 堆/栈水位采样、最小堆记录、串口健康日志 |

**优先级设计理由**
- InputTask 最高：人机交互系统对输入延迟最敏感，20ms 周期必须保证
- FileTask/MusicTask/LedTask 同级（2）：均为后台 I/O，无硬实时要求；同优先级由 FreeRTOS 时间片轮转
- MonitorTask 最低：日志打印和统计可延后，绝不抢占功能任务

### 任务间通信

| 通信通道 | 类型 | 生产者 | 消费者 | 说明 |
|---------|------|--------|--------|------|
| `file_req_queue` | 队列(4 深) | app_file (InputTask) | FileTask | 文件操作请求（READ/WRITE/CREATE/DELETE/RENAME/INIT） |
| `file_resp_queue` | 队列(4 深) | FileTask | app_file (InputTask) | 操作结果回传，app_file 非阻塞轮询 |
| `music_cmd_queue` | 队列(4 深) | app_music (InputTask) | MusicTask | 播放命令（LOAD/PLAY/PAUSE/RESUME/STOP/VOLUME） |

**设计理由**
- 队列实现自然解耦：InputTask 投递请求后立即返回，不等待 SD 卡 I/O 完成
- app_file 引入 `FS_WAIT` 子状态，每帧用 `FileTask_GetResponse(0)` 非阻塞检查，收到响应才推进状态机
- 队列长度 4 兼顾突发性与内存占用，队列水位通过 SYSMONITOR 可视化

### 共享资源同步保护

| 共享资源 | 同步原语 | 保护范围 | 加锁策略 |
|---------|---------|---------|---------|
| 日志缓冲区 `log_store` | 互斥量 `log_mutex` | add_entry / GetCount / GetEntry | 50ms 超时，调度器未启动时直写 |
| 系统设置 `settings` | 互斥量 `settings_mutex` | Settings_Save (Flash 擦写) | 100ms 超时，Flash 写入期间串行化 |
| DAC/音频输出 `dac` | 互斥量 `dac_mutex` | DAC_Start / DAC_Stop | 50ms 超时，TIM6 中断读 volatile 变量无需锁 |
| 文件表 `file_sys` | 隐式串行 | 所有 FileSys_* 调用 | 仅 FileTask 单任务访问，天然无竞争 |

**设计理由**
- 互斥量而非临界区：保护范围可能跨 SD 卡 I/O（数百 ms），临界区会禁用调度伤害实时性
- volatile + 单字节写：DAC 的 `volume`/`playing` 为单字节，ARM 单字节写原子，中断侧直接读 volatile 免锁
- 超时兜底：所有 `xSemaphoreTake` 均带超时，避免死锁时永久卡死任务

### 异常与边界处理

| 场景 | 处理策略 |
|------|---------|
| 栈溢出 | `configCHECK_FOR_STACK_OVERFLOW=2` + `vApplicationStackOverflowHook` 设置 `SYS_ERR_STACK` 并串口告警 |
| 堆分配失败 | `vApplicationMallocFailedHook` 设置 `SYS_ERR_HEAP` |
| SD 卡缺失 | FileTask 返回 `result<0`，桌面状态栏显示 `ERR:SD`，FILE 应用显示 `[NO SD!]` |
| 队列满 | `FileTask_Request` 返回 0，app_file 显示 `BUSY!` 提示 |
| 文件数满 | `FS_MAX_FILES=16` 检测，显示 `FULL!` |
| RTC 首次上电 | INITS 标志检测，设置 `SYS_ERR_RTC`，SETTIME 应用清除 |
| 输入设备断开 | PE6 ID 脚检测，设置 `SYS_ERR_INPUT`，状态栏显示 `ERR:INP` |
| 高频输入抖动 | 20ms 周期去抖 + `Monitor_IncDroppedEvent` 计数 |

### RTC 实时时钟
- LSE 32.768kHz 外部晶振（精度高）
- VBAT 纽扣电池维持掉电走时
- 首次上电自动检测 INITS 标志，避免覆盖已保存时间
- LSE 启动失败自动回退到 LSI
- **LSE 超时设为 1000ms**（非默认 5000ms），避免调度器启动前阻塞导致白屏
- RTC 初始化移至 InputTask（调度器启动后），避免 main.c 中 LSE 超时阻塞

### SD 卡文件系统
- Block 0 存储文件表（16 个文件条目）
- 每个文件 512 字节，8.3 文件名格式
- 支持创建/查看/编辑（字符网格）/删除/重命名

### DAC 音乐播放
- TIM6 定时中断 + DAC 输出方波
- 频率对应音高（C4~B5），占空比控制音量
- 音量设置掉电保存（Flash）

## 已知限制

1. **RTC 掉电保持依赖 VBAT 电池**：若 VBAT 引脚未接纽扣电池，掉电后时间会重置为 00:00:00
2. **输入设备检测**：PE6 ID 脚只能检测整个输入模块的接入/断开，无法检测单根线断开（需硬件增加独立 ID 脚）
3. **文件系统**：非 FAT 兼容，仅支持本项目自身的简单格式

## 开发笔记

- 移植 FreeRTOS 到 ARMCLANG (AC6) 需要将 port.c 的汇编从 AC5 语法改为 `__attribute__((naked))`
- `HAL_UART_MODULE_ENABLED` / `HAL_DAC_MODULE_ENABLED` / `HAL_RTC_MODULE_ENABLED` 必须在 `stm32f4xx_hal_conf.h` 中取消注释
- FreeRTOS 的 SysTick 和 PendSV 中断优先级必须设为 15（最低），`configMAX_SYSCALL_INTERRUPT_PRIORITY` = 0x50
- 新增 .c 文件需手动添加到 Keil 工程的编译组，否则会出现 Undefined symbol 链接错误
- HAL_RTC_GetTime 后必须紧跟 HAL_RTC_GetDate，否则时间寄存器会冻结

## 相关链接

- [STM32F407 参考手册](https://www.st.com/resource/en/reference_manual/dm00031020-stm32f405-415-stm32f407-417-stm32f427-437-stm32f429-439-advanced-arm-based-32-bit-mcus-stmicroelectronics.pdf)
- [FreeRTOS 官方文档](https://www.freertos.org/)
- [Keil MDK-ARM](https://www.keil.com/download/)

## License

本项目代码供学习和个人使用。ST HAL/CMSIS/FreeRTOS 部分遵循各自的原始许可证。
