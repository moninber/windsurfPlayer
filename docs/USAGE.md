# MediaStudio 使用文档

## 目录

1. [简介](#1-简介)
2. [系统要求](#2-系统要求)
3. [环境准备](#3-环境准备)
4. [编译步骤](#4-编译步骤)
5. [运行程序](#5-运行程序)
6. [功能说明](#6-功能说明)
7. [键盘快捷键](#7-键盘快捷键)
8. [数据库配置](#8-数据库配置)
9. [常见问题](#9-常见问题)
10. [技术支持](#10-技术支持)

---

## 1. 简介

MediaStudio 是一个基于 C++17 的桌面音视频播放器，采用 Qt 6.11 + OpenGL + FFmpeg + WASAPI 技术栈。

### 核心功能

- **视频播放**：支持多种视频格式（MP4, MKV, AVI, FLV 等）
- **音频播放**：支持多种音频格式（MP3, AAC, FLAC, WAV 等）
- **播放控制**：播放/暂停、停止、进度跳转、音量调节、倍速播放（0.25x - 4.0x）
- **视频特效**：OpenGL 着色器特效（正常、灰度、边缘检测、反色、模糊、亮度/对比度、怀旧）
- **音频可视化**：FFT 频谱分析，实时显示音频频谱柱状图
- **播放列表**：支持添加/删除文件、双击播放、上一首/下一首
- **媒体库**：MySQL 数据库存储媒体信息、收藏功能、播放历史（CMake/MinGW 默认关闭）
- **格式转码**：FFmpeg 编码 API 支持格式转换

---

## 2. 系统要求

### 硬件要求

- **操作系统**：Windows 10/11（64 位）
- **CPU**：支持 SSE2 指令集（Intel Core 2 Duo 或更高）
- **内存**：4GB RAM 以上（推荐 8GB）
- **显卡**：支持 OpenGL 3.3 Core Profile

### 软件要求

- **构建系统**：CMake + Ninja
- **编译器**：Qt 自带 MinGW 13.1.0（默认）
- **C++ 标准**：C++17
- **平台**：x64

---

## 3. 环境准备

### 3.1 必需软件

#### Qt 6.11

安装 Qt 6.11，并勾选 MinGW 64-bit、CMake、Ninja 组件。推荐保证 `cmake`、`ninja` 和 MinGW 编译器已经加入 `PATH`，或通过本地 `CMakeUserPresets.json` 显式指定。

```text
<qt-root>\
<mingw-root>\
<cmake-executable>
<ninja-executable>
```

#### FFmpeg 8.0

下载 FFmpeg 开发库并解压到本地目录：

```text
<ffmpeg-root>\
├── include\          ← 头文件（avcodec.h, avformat.h 等）
├── lib\              ← 库文件（*.lib / *.dll.a）
└── bin\              ← DLL 文件（运行时需要）
```

#### GLEW 2.3.1

下载 GLEW 并解压到本地目录：

```text
<glew-root>\
├── include\
├── lib\Release\x64\
└── bin\Release\x64\
    └── glew32.dll
```

#### MariaDB Connector/C（可选）

CMake/MinGW 构建默认关闭 MySQL：

```text
MEDIASTUDIO_ENABLE_MYSQL=OFF
```

如果需要启用数据库，请准备与当前编译器 ABI 兼容的 MariaDB Connector/C 或 MySQL C API 兼容库，并记录其本地路径：

```text
<mysql-connector-root>\
<mysql-server-root>\
```

---

## 4. 编译步骤

### 4.1 一键编译（推荐）

在项目根目录执行。若本地存在 `CMakeUserPresets.json`，脚本会优先使用 `mingw-release-local`；否则使用公开的 `mingw-release`：

```powershell
.\tools\build-mingw.ps1
```

脚本会自动：

- 使用当前环境中的 `cmake`
- 生成 Release 构建
- 在配置了依赖路径后调用 `windeployqt`
- 复制 FFmpeg/GLEW 运行时 DLL 到输出目录

输出文件：

```text
build\mingw-release\MediaStudio.exe
```

### 4.2 分步编译

```powershell
.\tools\configure-mingw.ps1
.\tools\build-mingw.ps1
```

### 4.3 手动 CMake 命令

```powershell
cmake --preset mingw-release `
  -DMEDIASTUDIO_QT_ROOT=<path-to-qt> `
  -DMEDIASTUDIO_FFMPEG_ROOT=<path-to-ffmpeg> `
  -DMEDIASTUDIO_GLEW_ROOT=<path-to-glew>

cmake --build --preset mingw-release
```

### 4.4 Debug 构建

```powershell
cmake --preset mingw-debug `
  -DMEDIASTUDIO_QT_ROOT=<path-to-qt> `
  -DMEDIASTUDIO_FFMPEG_ROOT=<path-to-ffmpeg> `
  -DMEDIASTUDIO_GLEW_ROOT=<path-to-glew>

cmake --build --preset mingw-debug
```

## 5. 运行程序

### 5.1 使用脚本运行

```powershell
.\tools\run-mingw.ps1
```

带媒体文件参数：

```powershell
.\tools\run-mingw.ps1 "D:\Videos\test.mp4"
```

### 5.2 直接运行

双击或命令行运行：

```text
build\mingw-release\MediaStudio.exe
```

然后通过菜单 **文件 → 打开** 加载媒体。

### 5.3 拖拽运行

直接将视频文件拖拽到 MediaStudio 窗口即可播放。

---

## 6. 功能说明

### 6.1 播放控制

| 功能 | 操作方式 | 说明 |
|------|---------|------|
| 播放/暂停 | 点击 ▶/⏸ 按钮或按 Space 键 | 切换播放状态 |
| 停止 | 点击 ■ 按钮或按 Esc 键 | 停止播放并关闭程序 |
| 进度跳转 | 拖动进度条 | 跳转到指定位置 |
| 音量调节 | 拖动音量滑块或按 ↑/↓ 键 | 调节音量（0-100%） |
| 倍速播放 | 下拉框选择全部档位，或按 1-4 数字键快速切换常用档位 | 数字键对应 0.5x, 1.0x, 1.5x, 2.0x |
| 静音/取消静音 | 按 M 键 | 快速切换静音状态 |
| 上一首 | 点击 ◀◀ 按钮或按 PageUp 键 | 播放列表上一首 |
| 下一首 | 点击 ▶▶ 按钮或按 PageDown 键 | 播放列表下一首 |

### 6.2 视频特效

通过菜单 **视图 → 切换特效** 或按 E 键切换特效：

1. **原始**：原始画面
2. **灰度**：黑白效果
3. **反色**：颜色反转
4. **边缘检测**：Sobel 算子边缘检测
5. **模糊**：盒式模糊
6. **亮度对比度**：调节亮度和对比度
7. **怀旧色调**：复古色调

### 6.3 音频可视化

通过菜单 **视图 → 切换频谱** 或按 V 键开启/关闭音频频谱可视化。

### 6.4 播放列表

- **添加文件**：菜单 **文件 → 添加到播放列表** 或拖拽文件到播放列表
- **删除文件**：选中播放列表项后点击“➖ 移除”按钮
- **播放文件**：双击播放列表项
- **清空列表**：点击“🗑 清空”按钮

### 6.5 媒体信息

右侧停靠面板显示当前播放文件的详细信息：

- 文件名
- 时长
- 视频编码、分辨率、帧率
- 音频编码、采样率、声道数
- 比特率

### 6.6 数据库功能（可选）

CMake/MinGW 默认构建中数据库功能关闭。此时菜单仍存在，但连接会提示“数据库功能未启用”。

如果启用 MySQL 构建：

1. 菜单 **工具 → 连接数据库**
2. 输入 MySQL 连接信息：
   - 主机地址：127.0.0.1
   - 用户名：root
   - 密码：你的 MySQL 密码
   - 数据库名：media_center
3. 点击确定

#### 收藏功能

- 按 F 键切换当前媒体的收藏状态
- 收藏的媒体在数据库中标记为 `is_favorite = 1`
- 播放列表中收藏的媒体会有特殊标识

#### 播放历史

- 每次播放自动记录到 `play_history` 表
- 包含播放时间戳和播放时长

### 6.7 格式转码（可选）

菜单 **工具 → 格式转码**：

1. 选择输入文件
2. 选择输出文件
3. 设置目标分辨率、编码器、码率
4. 点击开始转码

### 6.8 多线程播放架构（技术说明）

当前播放器采用三线程播放架构，以提升性能和音视频同步稳定性：

#### 线程架构

- **demux/control 线程**：读取压缩包并分发到音频/视频队列
- **音频工作线程**：从队列取包 → 解码 → 播放 → 更新音频主时钟
- **视频工作线程**：从队列取包 → 解码 → 根据音频主时钟同步 → 渲染

#### generation 机制（车次号）

为解决 seek/变速时的状态混乱，引入 generation 机制：

- **类比**：generation = 车次号，packet/frame = 乘客，音频/视频线程 = 接站的人
- **seek/变速时**：`stream_generation.fetch_add(1)` 换车次
- **旧包处理**：队列里的旧车次包被丢弃（`packet_generation != stream_generation`）
- **效果**：旧速度的包不会用新速度解码，避免音频忽快忽慢

#### 状态栏调试信息

播放器状态栏右侧会显示渲染/解码 FPS、A/V diff、累计丢帧、音频/视频队列和音频缓冲，主要用于快速判断播放是否稳、卡在哪里。

#### 音频主时钟

- **WASAPI 播放进度**：`audio_output_->getPlayedSeconds()` 返回已播放秒数
- **时钟计算**：`audio_clock_base + playedSeconds * active_speed`
- **视频同步**：视频帧根据音频主时钟等待/丢帧
- **纯音频文件**：使用墙时钟同步

---

## 7. 键盘快捷键

| 快捷键 | 功能 |
|--------|------|
| **Space** | 播放/暂停 |
| **Esc** | 退出程序 |
| **↑ / ↓** | 音量 + / - |
| **← / →** | 快退 / 快进 5 秒 |
| **PageUp / PageDown** | 上一首 / 下一首 |
| **1 - 4** | 切换倍速（0.5x, 1.0x, 1.5x, 2.0x） |
| **E** | 切换视频特效 |
| **V** | 切换音频可视化 |
| **F** | 切换收藏状态（需连接数据库） |
| **M** | 静音/取消静音 |
| **O** | 打开文件 |

---

## 8. 数据库配置

### 8.1 安装 MySQL

下载并安装 MySQL 8.0 Community Server：

```text
https://dev.mysql.com/downloads/mysql/
```

### 8.2 创建数据库

运行 `sql/init_database.sql`：

```bash
mysql -u root -p < sql/init_database.sql
```

### 8.3 启用 CMake 数据库构建

默认 CMake/MinGW 构建关闭数据库：

```text
MEDIASTUDIO_ENABLE_MYSQL=OFF
```

如果已经准备好兼容 MinGW 的 MySQL/MariaDB Connector，可以重新配置：

```powershell
cmake -S . -B build\mingw-release -G Ninja `
  -DCMAKE_PREFIX_PATH=<path-to-qt> `
  -DMEDIASTUDIO_QT_ROOT=<path-to-qt> `
  -DMEDIASTUDIO_ENABLE_MYSQL=ON `
  -DMEDIASTUDIO_MYSQL_CONNECTOR_ROOT=<path-to-mariadb-connector> `
  -DMEDIASTUDIO_MYSQL_SERVER_ROOT=<path-to-mysql-server>
```

### 8.4 在代码中自动连接数据库

编辑 `main.cpp`，取消以下行的注释并填入密码：

```cpp
window.connectDatabase("127.0.0.1", "root", "your_password", "media_center");
```

也可以不修改代码，在程序中通过菜单 **工具 → 连接数据库** 手动连接。

---

## 9. 常见问题

### 9.1 编译错误

| 错误信息 | 原因 | 解决方法 |
|---------|------|---------|
| `cmake 不是可识别的命令` | 系统 PATH 没有 CMake | 将 CMake 加入 PATH，或改用本机绝对路径执行 |
| `ninja 不是可识别的命令` | 系统 PATH 没有 Ninja | 将 Ninja 加入 PATH，或改用本机绝对路径执行 |
| `无法打开 avcodec.h` | FFmpeg 头文件路径未配置 | 检查 `MEDIASTUDIO_FFMPEG_ROOT` |
| `cannot find -l...` | 库文件路径或 ABI 不匹配 | 检查 FFmpeg/GLEW 是否为 x64 且与编译器兼容 |
| `mysql_driver.h` 找不到 | 数据库依赖未配置 | 默认关闭 MySQL，或设置 `MEDIASTUDIO_ENABLE_MYSQL=ON` 并配置路径 |

### 9.2 运行时错误

| 错误信息 | 原因 | 解决方法 |
|---------|------|---------|
| `找不到 avcodec-xx.dll` | FFmpeg DLL 未复制到 exe 目录 | 重新执行 `tools\build-mingw.ps1` |
| `找不到 Qt6Core.dll` | Qt DLL 未部署 | 重新执行 `tools\build-mingw.ps1`，让 `windeployqt` 部署 |
| `数据库功能未启用` | CMake 默认关闭 MySQL | 使用 `MEDIASTUDIO_ENABLE_MYSQL=ON` 重新配置 |
| `数据库连接失败` | MySQL 未启动或密码错误 | 检查 MySQL 服务状态，确认连接信息 |

### 9.3 播放问题

| 问题 | 可能原因 | 解决方法 |
|------|---------|---------|
| 画面卡顿 | 硬件解码未启用或分辨率过高 | 降低分辨率或更换更轻量的编码格式 |
| 音频不同步 | 倍速滤波器初始化失败 | 尝试 1.0x 倍速，检查 FFmpeg 日志 |
| 切换视频后卡顿 | 音频缓冲区残留 | 手动暂停/播放恢复，或重启程序 |

### 9.4 Qt/GLEW 警告

构建时可能看到：

```text
qopenglfunctions.h is not compatible with GLEW
```

当前项目仍可编译并链接成功。这个警告来自 `QOpenGLFunctions` 与 GLEW 同时使用。后续如果要彻底清理，可以统一改为只使用 Qt OpenGL 函数封装，或只使用 GLEW。

---

## 10. 技术支持

如遇到问题，请检查：

1. `cmake`、`ninja` 和编译器是否可用
2. `MEDIASTUDIO_QT_ROOT`、`MEDIASTUDIO_FFMPEG_ROOT`、`MEDIASTUDIO_GLEW_ROOT` 是否正确
3. Qt、FFmpeg、GLEW 是否与当前编译器和平台架构兼容
4. 输出目录 `build\mingw-release` 中是否已经部署运行时 DLL
