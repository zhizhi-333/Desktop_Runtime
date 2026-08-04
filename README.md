# STM32F407 智能人机界面系统

基于 STM32F407ZG + FreeRTOS 的嵌入式 HMI 系统，集成多任务调度、文件管理、绘图、音乐播放、系统监控等功能，并实现了看门狗保护、运行负载分析、存储一致性校验等进阶机制。

## 硬件平台

| 项目 | 规格 |
|------|------|
| MCU | STM32F407ZG @ 168MHz (Cortex-M4F) |
| Flash | 1MB（应用占用 Sector 11 存设置） |
| SRAM | 192KB（FreeRTOS 堆 48KB） |
| 显示 | 480×320 TFT LCD (SPI) |
| 输入 | 矩阵键盘 + 旋转编码器 (TIM4) |
| 存储 | microSD 卡 (SDIO, 裸块文件系统) |
| 音频 | DAC 输出 + LM386 功放 |
| 日历 | RTC (LSE 32.768kHz, VBAT 备份) |
| 通信 | USART3 (PD8/PD9, 115200, 日志输出) |

## 功能特性

### 基础功能
- **多任务架构**：5 个 FreeRTOS 任务（Input/Monitor/LED/File/Music），优先级分层
- **登录系统**：开机密码登录，支持锁屏/解锁
- **桌面启动器**：图标网格布局，支持拖拽重排
- **文件管理**：SD 卡裸块文件系统，16 文件 × 512B，支持文本/绘图/配置类型
- **绘图应用**：画板作图，可保存为文件供后续查看
- **音乐播放**：DAC 输出方波旋律，后台任务非阻塞播放
- **系统设置**：光标灵敏度/大小、亮度、音量、熄屏时间，Flash 掉电保持
- **时间设置**：RTC 读写，首次上电检测
- **日志系统**：环形缓冲区日志，串口实时输出 + 历史查看

### 进阶功能（本版本新增）
- **看门狗 + 任务心跳**：IWDG 硬件看门狗 + 5 任务软件心跳检测，任务卡死 5 秒自动复位
- **运行负载分析**：记录各任务栈历史最小值（最大使用量）和队列历史峰值，SYSMONITOR 第二页可视化
- **存储一致性保护**：设置数据 CRC32 校验（Flash）+ 文件表 CRC32 校验（SD 卡 Block 17），掉电损坏自动恢复

## 软件架构

### 任务划分与优先级

| 任务 | 优先级 | 栈大小 | 职责 |
|------|--------|--------|------|
| InputTask | 3 (最高) | 512W | 键盘/编码器扫描、UI 刷新、状态机 |
| FileTask | 2 | 1024W | SD 卡文件 I/O（异步队列） |
| MusicTask | 2 | 512W | DAC 音乐播放推进（20ms 周期） |
| LedTask | 2 | 256W | 心跳指示灯（500ms 翻转） |
| MonitorTask | 1 (最低) | 1024W | 系统监控、看门狗喂狗、负载采样 |

### 任务间通信
- **队列**：FileTask 请求/响应队列、MusicTask 命令队列（异步解耦，避免阻塞 InputTask）
- **互斥量**：`log_mutex`（日志）、`settings_mutex`（设置）、`dac_mutex`（DAC 输出）

### 状态机
```
BOOT (2s 启动画面)
  └─ LOGIN (密码界面)
       └─ DESKTOP (桌面)
            └─ APP (应用运行)
                 └─ 熄屏/锁屏 (BACK/编码器按压触发)
```

## 目录结构

```
microcomputer/
├── Core/                   # STM32CubeMX 生成核心
│   ├── Inc/                # FreeRTOSConfig.h, stm32f4xx_hal_conf.h 等
│   └── Src/                # main.c, gpio.c, usart.c 等
├── Drivers/                # HAL 库 + CMSIS
├── MDK-ARM/                # Keil 工程
│   └── microcomputer.uvprojx
├── User/
│   ├── app/                # 应用层
│   │   ├── monitor.c/h     # 监控任务 + 看门狗 + 心跳 + 负载分析
│   │   ├── file_task.c/h   # 文件后台任务
│   │   ├── music_task.c/h  # 音乐后台任务
│   │   ├── file_sys.c/h    # SD 卡文件系统 + CRC 校验
│   │   ├── settings.c/h    # 系统设置 + Flash CRC 校验
│   │   ├── app_sysmonitor.c# 系统监控应用 (双页: 实时/历史)
│   │   ├── app_draw.c/h    # 绘图应用
│   │   ├── app_file.c      # 文件管理应用
│   │   ├── app_music.c     # 音乐播放应用
│   │   ├── app_settings.c  # 设置应用
│   │   ├── app_settime.c   # 时间设置应用
│   │   ├── app_manager.c/h # 应用注册表与切换
│   │   ├── desktop.c/h     # 桌面启动器
│   │   ├── login.c/h       # 登录/锁屏
│   │   └── log_store.c/h   # 日志环形缓冲
│   └── dev/                # 驱动层 (LCD/GUI/Key/RTC/DAC)
└── .gitignore
```

## 编译与烧录

1. 用 Keil MDK-ARM V5 打开 `MDK-ARM/microcomputer.uvprojx`
2. 编译器：ARMCLANG (AC6) V6.18
3. Rebuild（0 error, 0 warning 为正常）
4. 烧录工具：ST-Link / J-Link

> **注意**：添加新 `.c` 文件后需手动加入 Keil 工程编译组，否则链接报 Undefined symbol。

## 使用说明

1. 上电后显示启动画面 2 秒，进入登录界面
2. 输入密码进入桌面（默认密码见 login.c）
3. 桌面选择应用图标，按 OK 进入
4. **SYSMONITOR 应用**：按 OK 切换实时数据/历史峰值两页
5. BACK 键返回桌面；桌面下按 BACK 熄屏

## 串口日志

USART3 (PD8/PD9), 115200, 8N1。关键日志标签：

| 标签 | 含义 |
|------|------|
| `[BOOT]` | 启动流程 |
| `[WDT]` | 看门狗初始化/卡死检测 |
| `[SET]` | 设置读写（含 CRC 校验结果） |
| `[FS]` | 文件系统（含表 CRC 校验结果） |
| `[MON]` | 监控数据采样 |
| `[INPUT]` | 输入任务状态变化 |
| `[FILE]` | 文件任务操作 |
| `[MUSIC]` | 音乐任务操作 |
| `[RTC]` | RTC 初始化 |

## 进阶功能验证

首次烧录后串口会出现（属正常）：
```
[SET] CRC mismatch! flash data corrupted, using defaults
[FS] table CRC not present (first use), will create
[WDT] IWDG initialized (timeout ~12.5s)
```

之后正常运行时这些告警不再出现（数据已建立 CRC 并校验通过）。
