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

---

## 1. 简介

MediaStudio 是一个基于 C++17 的桌面音视频播放器，采用 Qt 6.11 + OpenGL + FFmpeg + WASAPI 技术栈。

### 核心功能

- **视频播放**：支持多种视频格式（MP4, MKV, AVI, FLV 等）
- **音频播放**：支持多种音频格式（MP3, AAC, FLAC, WAV 等）
- **播放控制**：播放/暂停、停止、进度跳转、音量调节、倍速播放（0.25x - 4.0x）
- **视频特效**：7 种 OpenGL 着色器特效（正常、灰度、边缘检测、反色、模糊、锐化、怀旧）
- **音频可视化**：FFT 频谱分析，实时显示音频频谱柱状图
- **播放列表**：支持添加/删除文件、双击播放、上一首/下一首
- **媒体库**：MySQL 数据库存储媒体信息、收藏功能、播放历史
- **格式转码**：FFmpeg 编码 API 支持格式转换

---

## 2. 系统要求

### 硬件要求

- **操作系统**：Windows 10/11（64 位）
- **CPU**：支持 SSE2 指令集（Intel Core 2 Duo 或更高）
- **内存**：4GB RAM 以上（推荐 8GB）
- **显卡**：支持 OpenGL 3.3 Core Profile

### 软件要求

- **编译器**：Visual Studio 2022（MSVC v143）
- **C++ 标准**：C++17
- **平台**：x64

---

## 3. 环境准备

### 3.1 必需软件

#### Visual Studio 2022
- 安装 **"使用 C++ 的桌面开发"** 工作负载
- 确保 MSVC v143 编译器已安装

#### FFmpeg 8.0
下载 FFmpeg 开发库并解压到指定目录：

```
E:\ffmpeg\
├── include\          ← 头文件（avcodec.h, avformat.h 等）
├── lib\              ← 库文件（avcodec.lib, avformat.lib 等）
└── bin\              ← DLL 文件（运行时需要）
    ├── avcodec.dll
    ├── avformat.dll
    ├── avutil.dll
    ├── swscale.dll
    ├── swresample.dll
    └── avfilter.dll
```

#### Qt 6.11
下载 Qt 6.11 for Windows (MSVC 2022 64-bit)：

```
E:\Qt\6.11.0\msvc2022_64\
├── include\          ← Qt 头文件
├── lib\              ← Qt 库文件
└── bin\              ← Qt DLL（运行时需要）
```

#### GLEW 2.3.1
下载 GLEW 并解压：

```
E:\OpenGl\glew-2.3.1\
└── bin\Release\x64\
    └── glew32.dll    ← 运行时需要
```

#### MySQL Connector/C++ 8.0（可选，用于媒体库功能）
下载 MySQL Connector/C++ 并解压：

```
E:\mysql-connector-c++\
├── include\          ← 头文件
├── lib64\            ← 库文件
└── bin\              ← DLL 文件（运行时需要）
    ├── mysqlcppconn.dll
    └── mysqlcppconn8.dll
```

### 3.2 环境变量配置（可选）

如果不想将 DLL 复制到 exe 目录，可以添加到 PATH：

```
PATH=%PATH%;E:\ffmpeg\bin;E:\OpenGl\glew-2.3.1\bin\Release\x64;E:\Qt\6.11.0\msvc2022_64\bin
```

---

## 4. 编译步骤

### 4.1 打开项目

1. 启动 Visual Studio 2022
2. 打开 `MediaStudio.sln`
3. 确认配置为 **Debug | x64** 或 **Release | x64**

### 4.2 检查项目配置

打开 `MediaStudio.vcxproj`，确认以下路径正确：

#### 包含目录（AdditionalIncludeDirectories）
```
E:\ffmpeg\include;
E:\OpenGl\glew-2.3.1\include;
E:\Qt\6.11.0\msvc2022_64\include;
E:\Qt\6.11.0\msvc2022_64\include\QtCore;
E:\Qt\6.11.0\msvc2022_64\include\QtGui;
E:\Qt\6.11.0\msvc2022_64\include\QtWidgets;
E:\Qt\6.11.0\msvc2022_64\include\QtOpenGLWidgets;
E:\mysql-connector-c++\include;  （可选）
```

#### 库目录（AdditionalLibraryDirectories）
```
E:\ffmpeg\lib;
E:\OpenGl\glew-2.3.1\lib\Release\x64;
E:\Qt\6.11.0\msvc2022_64\lib;
E:\mysql-connector-c++\lib64;  （可选）
```

#### 依赖库（AdditionalDependencies）
```
avcodec.lib;avformat.lib;avutil.lib;swscale.lib;swresample.lib;avfilter.lib;
glew32.lib;
opengl32.lib;
Qt6Core.lib;Qt6Gui.lib;Qt6Widgets.lib;Qt6OpenGLWidgets.lib;
mysqlcppconn.lib;  （可选）
```

### 4.3 编译

1. 右键项目 → **生成**
2. 等待编译完成
3. 输出目录：`x64\Debug\MediaStudio.exe` 或 `x64\Release\MediaStudio.exe`

### 4.4 复制运行时 DLL

将以下 DLL 复制到 exe 目录：

#### FFmpeg DLL
```
avcodec.dll
avformat.dll
avutil.dll
swscale.dll
swresample.dll
avfilter.dll
```

#### GLEW DLL
```
glew32.dll
```

#### Qt 6 DLL
```
Qt6Core.dll
Qt6Gui.dll
Qt6Widgets.dll
Qt6OpenGLWidgets.dll
Qt6OpenGL.dll
```

#### MySQL Connector/C++ DLL（可选）
```
mysqlcppconn.dll
mysqlcppconn8.dll
libmysql.dll
```

#### OpenSSL DLL（MySQL Connector 依赖，可选）
```
libssl-1_1-x64.dll
libcrypto-1_1-x64.dll
```

---

## 5. 运行程序

### 5.1 命令行运行

```bash
MediaStudio.exe [视频文件路径1] [视频文件路径2] ...
```

示例：
```bash
MediaStudio.exe video.mp4
MediaStudio.exe video1.mp4 video2.mkv video3.avi
```

### 5.2 直接运行

双击 `MediaStudio.exe`，然后通过菜单 **文件 → 打开** 加载媒体。

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

通过菜单 **视图 → 切换特效** 或按 E 键切换 7 种特效：

1. **正常**：原始画面
2. **灰度**：黑白效果
3. **边缘检测**：Sobel 算子边缘检测
4. **反色**：颜色反转
5. **模糊**：高斯模糊
6. **锐化**：边缘增强
7. **怀旧**：复古色调

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

#### 连接数据库
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
- https://dev.mysql.com/downloads/mysql/

### 8.2 创建数据库

运行 `sql/init_database.sql`：

```bash
mysql -u root -p < sql/init_database.sql
```

或手动执行以下 SQL：

```sql
CREATE DATABASE media_center CHARACTER SET utf8mb4 COLLATE utf8mb4_unicode_ci;

USE media_center;

CREATE TABLE media_library (
    id INT AUTO_INCREMENT PRIMARY KEY,
    file_path VARCHAR(1024) NOT NULL UNIQUE,
    title VARCHAR(512),
    duration INT,
    video_codec VARCHAR(64),
    video_width INT,
    video_height INT,
    video_fps DECIMAL(10,2),
    audio_codec VARCHAR(64),
    audio_sample_rate INT,
    audio_channels INT,
    bitrate INT,
    is_favorite BOOLEAN DEFAULT FALSE,
    tags VARCHAR(512),
    created_at TIMESTAMP DEFAULT CURRENT_TIMESTAMP,
    updated_at TIMESTAMP DEFAULT CURRENT_TIMESTAMP ON UPDATE CURRENT_TIMESTAMP
);

CREATE TABLE play_history (
    id INT AUTO_INCREMENT PRIMARY KEY,
    media_id INT,
    played_at TIMESTAMP DEFAULT CURRENT_TIMESTAMP,
    play_duration INT,
    FOREIGN KEY (media_id) REFERENCES media_library(id) ON DELETE CASCADE
);
```

### 8.3 在代码中启用数据库

编辑 `main.cpp`，取消以下行的注释并填入密码：

```cpp
window.connectDatabase("127.0.0.1", "root", "your_password", "media_center");
```

---

## 9. 常见问题

### 9.1 编译错误

| 错误信息 | 原因 | 解决方法 |
|---------|------|---------|
| `无法打开 avcodec.h` | FFmpeg 头文件路径未配置 | 检查 `vcxproj` → `AdditionalIncludeDirectories` |
| `无法解析的外部符号 avformat_open_input` | FFmpeg lib 未链接 | 检查 `AdditionalDependencies` 添加 FFmpeg 库 |
| `glewInit 失败` | GLEW DLL 不在 PATH | 复制 `glew32.dll` 到 exe 目录 |
| `Qt 相关编译错误` | Qt 版本/路径不匹配 | 确认 Qt 6.11 MSVC2022 64-bit，检查 `QTDIR` 环境变量 |

### 9.2 运行时错误

| 错误信息 | 原因 | 解决方法 |
|---------|------|---------|
| `找不到 avcodec.dll` | FFmpeg DLL 未复制到 exe 目录 | 复制 FFmpeg DLL 到 exe 目录或添加到 PATH |
| `找不到 Qt6Core.dll` | Qt DLL 未复制到 exe 目录 | 复制 Qt DLL 到 exe 目录或添加到 PATH |
| `数据库连接失败` | MySQL 未启动或密码错误 | 检查 MySQL 服务状态，确认连接信息 |
| `Debug 版本数据库崩溃` | Debug/Release 二进制不兼容 | 使用 Release 版本运行 |

### 9.3 播放问题

| 问题 | 可能原因 | 解决方法 |
|------|---------|---------|
| 画面卡顿 | 硬件解码未启用或分辨率过高 | 降低分辨率或更换更轻量的编码格式 |
| 音频不同步 | 倍速滤波器初始化失败 | 尝试 1.0x 倍速，检查 FFmpeg 日志 |
| 切换视频后卡顿 | 音频缓冲区残留 | 手动暂停/播放恢复，或重启程序 |

### 9.4 Debug 与 Release 模式

**重要提示**：由于 MySQL Connector/C++ 和 FFmpeg 库是 Release 版编译的，Debug 模式下连接数据库可能会出现 STL 二进制不兼容导致的崩溃。

**建议**：
- 开发调试时：使用 Release 模式 + 断点调试
- 如果必须使用 Debug 模式：重新编译所有依赖库为 Debug 版本

---

## 10. 技术支持

如遇到问题，请检查：
1. 所有 DLL 是否在正确位置
2. 环境变量 PATH 是否配置正确
3. Visual Studio 配置是否正确
4. FFmpeg/Qt/GLEW 版本是否匹配

项目源码地址：
https:  https://github.com/moninber/windsurfPlayer.git
ssh:   git@github.com:moninber/windsurfPlayer.git