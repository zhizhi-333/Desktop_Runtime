# 基于 FreeRTOS 的微型桌面运行时系统

> 2026 电子科技协会嵌入式组考核项目
>
> 用屏幕模拟电脑显示器，用矩阵键盘 + 旋转编码器模拟鼠标，在 STM32F407 + FreeRTOS 上实现
> 启动登录、桌面、文件管理、绘图、音乐、日志、系统监控、系统设置等应用。重点不是做操作系统，
> 而是体现 **FreeRTOS 多任务协作、实时响应、共享资源保护、运行监控与异常恢复**。

---

## 硬件平台

| 项目 | 规格 |
|------|------|
| MCU | STM32F407ZG @ 168 MHz (Cortex-M4F)，1 MB Flash，192 KB SRAM |
| 显示 | 3.5 英寸 ILI9488 TFT LCD，480×320，4 线 SPI（SPI1 + DC/CS/RST） |
| 输入 | 4×4 矩阵键盘 + EC11 旋转编码器（TIM4 编码器模式），可拔插，PE6 ID 脚断连检测 |
| 存储 | MicroSD（SDIO 4-bit，裸块文件系统，16 文件 × 512B）+ 内部 Flash Sector 11（系统设置） |
| 音频 | DAC1（PA4）+ TIM6 触发 + PAM8403 功放 + 扬声器 |
| 时钟 | RTC（LSE 32.768 kHz，VBAT 备份） |
| 通信 | USART3（PD8/PD9，115200，日志输出） |
| 可靠性 | 独立看门狗 IWDG（~12.5 s）+ 任务软件心跳（5 s 判死） |

## 功能特性

### 基础功能
- **多任务架构**：5 个 FreeRTOS 任务，优先级分层 + 时间片轮转
- **登录系统**：开机密码登录，连续 3 次错误锁定 30 s，支持锁屏/解锁
- **桌面启动器**：图标网格，光标移动/选中/打开，应用最小化到任务栏
- **文件管理**：SD 卡裸块文件系统，新建/查看/编辑/删除/重命名 + 重名检测，掉电保持
- **绘图应用**：画板作图、7 色、清空、保存到 SD 卡，重新上电可恢复
- **音乐播放**：DAC 方波旋律，3 首预置曲目，播放/暂停/停止/切歌/音量，最小化后台继续播放
- **系统设置**：光标灵敏度/大小、亮度、音量、熄屏时间，Flash 掉电保持 + CRC 校验
- **时间设置**：RTC 读写，首次上电检测
- **日志系统**：环形缓冲区日志 + 串口实时输出 + 历史查看 + 清空
- **熄屏/锁屏**：主动熄屏（BACK）、超时熄屏、编码器按压锁屏；输入唤醒不复位 MCU
- **输入断连检测**：PE6 ID 脚识别输入器拔插，状态栏显示 `ERR:INP`

### 进阶功能
- **看门狗 + 任务心跳**：IWDG 硬件看门狗 + 5 任务软件心跳，任务卡死 5 s 停止喂狗 → 12.5 s 硬件复位
- **运行负载分析**：各任务栈历史最小值（最大使用量）+ 队列历史峰值，SYSMONITOR 第二页可视化
- **存储一致性保护**：设置数据 CRC32（Flash）+ 文件表 CRC32（SD 卡 Block 17），掉电损坏自动回退
- **栈溢出 / 堆失败钩子**：`configCHECK_FOR_STACK_OVERFLOW=2`，运行时检测并标记错误

## 软件架构

### 任务划分与优先级

| 任务 | 优先级 | 栈大小 | 周期/触发 | 职责 |
|------|--------|--------|----------|------|
| InputTask | 3（最高） | 512W | 20 ms | 矩阵键盘/编码器扫描、系统状态机、UI 刷新、应用事件分发 |
| FileTask | 2 | 1024W | 队列阻塞（1 s 超时） | SD 卡文件 I/O，异步队列，回传结果 |
| MusicTask | 2 | 512W | 20 ms | DAC 音乐播放推进，命令队列控制 |
| LedTask | 2 | 256W | 500 ms | 心跳指示灯 |
| MonitorTask | 1（最低） | 1024W | 1000 ms | 堆/栈水位采样、错误计数、看门狗喂狗、负载分析 |

### 任务间通信
- **队列**：`file_req_queue` / `file_resp_queue`（请求-响应双队列）、`music_cmd_queue`，各长 4
- **互斥量**：`log_mutex`（日志）、`settings_mutex`（Flash 设置）、`dac_mutex`（DAC 启停），均带超时 + 优先级继承

### 系统状态机
```
BOOT (2s 启动画面)
  └─ LOGIN (密码界面，3 错锁 30s)
       └─ RUNTIME (桌面 + 应用)
            └─ SCREEN_OFF (熄屏，BACK 触发，唤醒无密码)
            └─ LOCKED   (锁屏，编码器按压触发，唤醒需密码)
```

### 数据流
```
矩阵键盘/编码器 ──Key_Scan──> InputTask ──(file_req)──> FileTask ──(file_resp)──> InputTask
                                   └────(music_cmd)──> MusicTask
MonitorTask: 每秒采样堆/栈/队列/心跳，健康则喂 IWDG
```

## 目录结构

```
microcomputer/
├── Core/                   # STM32CubeMX 生成
│   ├── Inc/                # FreeRTOSConfig.h, stm32f4xx_hal_conf.h, main.h ...
│   └── Src/                # main.c, gpio.c, spi.c, tim.c, usart.c ...
├── Drivers/                # STM32 HAL 库 + CMSIS
├── FreeRTOS/               # FreeRTOS 内核（ARM_CM4F 移植）
├── MDK-ARM/                # Keil 工程
│   └── microcomputer.uvprojx
├── User/
│   ├── dev/                # 设备驱动层
│   │   ├── lcd.c/h         # ILI9488 4线SPI 驱动
│   │   ├── gui.c/h         # 自研 GUI 绘图接口
│   │   ├── key.c/h         # 4×4 矩阵键盘 + ID 断连检测
│   │   ├── encoder.c/h     # EC11 编码器（TIM4）
│   │   ├── dac.c/h         # DAC1 + TIM6 音频输出
│   │   └── rtc_time.c/h    # RTC 时间接口
│   └── app/                # 应用 + 服务层
│       ├── monitor.c/h     # 任务管理 + 看门狗 + 心跳 + 负载分析 + InputTask
│       ├── app_manager.c/h # 应用注册表与最小化/恢复
│       ├── desktop.c/h     # 桌面 + 任务栏
│       ├── login.c/h       # 登录/锁屏
│       ├── file_sys.c/h    # SD 卡裸块文件系统 + CRC
│       ├── file_task.c/h   # 文件后台任务
│       ├── music.c/h       # 旋律引擎
│       ├── music_task.c/h  # 音乐后台任务
│       ├── settings.c/h    # 系统设置 + Flash CRC
│       ├── log_store.c/h   # 日志环形缓冲
│       ├── app_file.c      # 文件管理应用
│       ├── app_draw.c      # 绘图应用
│       ├── app_music.c     # 音乐播放应用
│       ├── app_log.c       # 日志查看应用
│       ├── app_sysmonitor.c# 系统监控应用（实时/历史双页）
│       ├── app_settings.c  # 系统设置应用
│       └── app_settime.c   # 时间设置应用
├── docs/                   # 项目文档
│   ├── 开发文档.md         # 多任务架构设计理由
│   ├── 开发日志.md         # bug 排查与修复记录
│   └── 操作指南.md         # 操作手册（小白版）
└── README.md
```

## 编译与烧录

1. 用 Keil MDK-ARM V5 打开 `MDK-ARM/microcomputer.uvprojx`
2. 编译器：ARMCLANG (AC6)，推荐 V6.18 及以上
3. Rebuild（0 error, 0 warning 为正常）
4. 烧录工具：ST-Link / J-Link（SWD）

> **注意**：新增 `.c` 文件后需手动加入 Keil 工程编译组，否则链接报 Undefined symbol。
> 新增外设时需在 `Core/Inc/stm32f4xx_hal_conf.h` 取消注释对应 `#define HAL_xxx_MODULE_ENABLED`
> 并把 HAL 源文件加入编译组（IWDG/DAC/RTC 等均遇到过此问题，详见开发日志）。

## 使用说明

1. 上电 → 启动画面 2 秒 → 登录界面（默认密码 `1234`）
2. 方向键移动光标，OK 确认，BACK 返回/熄屏，编码器旋转滚动/调音量，编码器按压最小化/锁屏
3. 桌面 6 个应用图标：FILE / DRAW / MUSIC / LOG / MONITOR / CONFIG（另 SETTIME）
4. MONITOR 应用按 OK 切换实时数据 / 历史峰值两页

详细操作见 [`docs/操作指南.md`](docs/操作指南.md)。

## 串口日志

USART3（PD8-TX / PD9-RX），115200, 8N1。主要标签：

| 标签 | 含义 |
|------|------|
| `[BOOT]` | 启动流程 |
| `[WDT]` | 看门狗初始化 / 卡死检测 |
| `[SET]` | 设置读写（含 CRC 校验结果） |
| `[FS]` | 文件系统（含表 CRC 校验结果） |
| `[MON]` | 监控数据采样 |
| `[INPUT]` | 输入任务状态变化（含设备拔插） |
| `[FILE]` / `[MUSIC]` / `[DAC]` / `[RTC]` | 各模块运行日志 |

## 文档

- [`docs/开发文档.md`](docs/开发文档.md) — 多任务架构、优先级、队列、互斥量、异常处理的设计理由
- [`docs/开发日志.md`](docs/开发日志.md) — 开发过程中遇到的 bug、排查过程与解决方式
- [`docs/操作指南.md`](docs/操作指南.md) — 面向用户的完整操作手册与硬件排障

## 考核要求对照

| 考核要求 | 实现情况 |
|---------|---------|
| 启动/登录/连续错误锁定 | ✅ 3 次错误锁 30 s |
| 桌面/光标/选中/打开/退出 | ✅ |
| 输入设备断开提示 + 恢复不重启 | ✅ PE6 ID 脚检测 |
| 主动/超时熄屏 + 输入唤醒 | ✅ 逻辑熄屏，不复位 MCU |
| 6 个应用真实运行 | ✅ 文件/绘图/音乐/日志/监控/设置 |
| FreeRTOS 多任务协作 | ✅ 5 任务 + 队列 + 互斥量 |
| 共享资源保护 | ✅ 互斥量 + 超时 + 优先级继承 |
| 监控显示栈/堆/队列/错误 | ✅ SYSMONITOR 双页 |
| 设置立即生效 + 重启保持 | ✅ Flash + CRC |
| 画图断电保留 | ✅ DRAW.BIN 存 SD |
| 进阶：任务异常检测恢复 | ✅ 心跳 + IWDG 双层 |
| 进阶：存储一致性 | ✅ Flash + SD 双 CRC |
| 进阶：运行负载分析 | ✅ 栈最小值 + 队列峰值 |

## 已知待完善项

- 背光目前为 GPIO 开关，亮度调节（PWM）未实现
- 日志当前仅在 RAM 环形缓冲 + 串口，未持久化到 SD 卡
- 配置为单槽 + CRC，未做 A/B 双槽无缝切换
- 低功耗 Stop 模式、OTA 未实现

## 技术栈

- STM32CubeMX + HAL 驱动 + FreeRTOS 原生 API
- 自研 GUI（非 LVGL）、自研裸块文件系统（非 FatFs）
- ARMCLANG (AC6) 工具链

---

*项目：STM32F407ZG 智能终端 · 操作系统：FreeRTOS · 2026*
