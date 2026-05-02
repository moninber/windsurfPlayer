# MediaStudio 学习使用文档

## 目录

1. [项目快速启动](#1-项目快速启动)
2. [项目阅读顺序与架构](#2-项目阅读顺序与架构)
3. [FFmpeg音视频处理](#3-ffmpeg音视频处理)
4. [OpenGL视频渲染](#4-opengl视频渲染)
5. [音频处理与可视化](#5-音频处理与可视化)
6. [Qt GUI与多线程](#6-qt-gui与多线程)
7. [MySQL数据库集成](#7-mysql数据库集成)
8. [媒体转码](#8-媒体转码)
9. [问题排查与调试思路](#9-问题排查与调试思路)
10. [核心概念速查表](#10-核心概念速查表)

---

## 1. 项目快速启动

### 1.1 环境准备

**必需软件**：
- Visual Studio 2022（含C++桌面开发工作负载）
- FFmpeg 8.0 开发库（头文件 + lib + DLL）
- Qt 6.11（MSVC2022 64-bit）

**目录结构要求**：
```
E:\ffmpeg\include\         ← FFmpeg头文件
E:\ffmpeg\lib\             ← FFmpeg库文件
E:\ffmpeg\bin\             ← FFmpeg DLL（运行时必需）

E:\OpenGl\glew-2.3.1...\   ← GLEW
E:\Qt\6.11.0\...            ← Qt安装路径（需在PATH或vcxproj中配置）
```

### 1.2 编译步骤

1. 用VS2022打开 `MediaStudio.sln`
2. 确认配置为 **Debug | x64**
3. 在 `MediaStudio.vcxproj` 中确认 Qt 和 FFmpeg 包含路径正确
4. 右键项目 → **生成**
5. 将以下DLL复制到 `x64/Debug/` 目录：
   - `E:\ffmpeg\bin\avcodec.dll`, `avformat.dll`, `avutil.dll`, `swscale.dll`, `swresample.dll`, `avfilter.dll`
   - `E:\OpenGl\glew-...\bin\Release\x64\glew32.dll`
   - Qt 6 核心 DLL：`Qt6Core.dll`, `Qt6Gui.dll`, `Qt6Widgets.dll`, `Qt6OpenGLWidgets.dll`

### 1.3 运行

```
MediaStudio.exe [视频文件路径]
```

或直接双击运行，通过菜单 **文件 → 打开** 加载媒体。

---

## 2. 项目阅读顺序与架构

### 2.1 建议阅读顺序（由浅入深）

阅读代码时，**顺着数据流走**最容易理解：

| 顺序 | 文件 | 作用 | 需要关注的重点 |
|------|------|------|-------------|
| 1 | `main.cpp` | 程序入口 | `QApplication` 初始化、`MainWindow` 创建、命令行参数处理 |
| 2 | `MainWindow.h` | GUI 声明 | 了解有哪些 UI 控件、信号槽、核心成员变量 |
| 3 | `MainWindow.cpp` | GUI 实现 + 事件处理 | `requestLoadAndPlay()` 的异步加载逻辑、`QMetaObject::invokeMethod` 跨线程更新 UI |
| 4 | `PlayerController.h/.cpp` | 播放控制枢纽 | `loadFile()` / `play()` / `stop()` 的生命周期、`playbackThread()` 主循环、`session_id_` 的作用 |
| 5 | `MediaDecoder.h/.cpp` | FFmpeg 解码 | `open()` / `close()` / `decodeNextFrame()`、`AVFilterGraph` 的 `atempo` 集成、`sws_scale` 色彩转换 |
| 6 | `AudioOutput.h/.cpp` | WASAPI 音频输出 | `init()` 六步流程、`playPCM()` 缓冲区写入、`pause()` / `resume()` |
| 7 | `VideoWidget.h/.cpp` | Qt OpenGL 视频显示 | `QOpenGLWidget` 三件套（`initializeGL` / `paintGL` / `resizeGL`）、帧缓冲互斥锁、`clearFrame()` |
| 8 | `VideoRenderer.h/.cpp` | OpenGL 渲染器 | 着色器编译、纹理上传、`glTexSubImage2D`、7 种 GLSL 特效 |
| 9 | `AudioVisualizer.h/.cpp` | FFT 频谱可视化 | `fft()` 的 Cooley-Tukey 实现、窗函数、分贝归一化 |
| 10 | `MediaInfo.h` | 数据结构 | `MediaInfo`、`VideoStreamInfo`、`AudioStreamInfo`、`VideoEffect` 枚举 |
| 11 | `FrameQueue.h` | 线程安全队列 | `std::mutex` + `std::condition_variable` 的生产者-消费者模板 |
| 12 | `MediaDatabase.h/.cpp` | MySQL 媒体库 | `PreparedStatement`、数据库表设计、CRUD 操作 |
| 13 | `MediaTranscoder.h/.cpp` | 格式转码 | FFmpeg 编码 API 的完整链路 |
| 14 | `Application.h/.cpp` | 旧 GLFW 版本 | 已废弃，但可作为对比学习 GLFW 与 Qt 的差异 |

### 2.2 架构理解口诀

- **一条主线**：`MainWindow` 收到用户操作 → 调用 `PlayerController` → `MediaDecoder` 解码 → `AudioOutput` 播放音频 + `VideoWidget` 显示视频
- **两个线程**：Qt 主线程（GUI + OpenGL）+ `playbackThread`（解码 + 音频输出 + 帧推送）
- **三道关卡**：`load_request_id_`（UI 层去重）→ `lifecycle_mutex_`（控制器层互斥）→ `session_id_`（播放线程层会话隔离）

---

## 3. FFmpeg音视频处理

### 2.1 核心概念

FFmpeg处理媒体文件的核心数据流：

```
媒体文件 → AVPacket(压缩包) → AVFrame(原始帧) → 像素/音频数据
         解封装(av_read_frame)    解码(avcodec_send/receive)
```

**四个核心结构体**：

| 结构体 | 含义 | 类比 |
|--------|------|------|
| `AVFormatContext` | 文件格式上下文 | 书的目录 |
| `AVCodecContext` | 编解码器上下文 | 翻译官 |
| `AVPacket` | 压缩数据包 | 压缩的包裹 |
| `AVFrame` | 解码后的帧 | 打开的包裹 |

### 2.2 代码学习路线

**第一步**：阅读 `MediaDecoder.cpp` 的 `open()` 函数

```cpp
// 这6行代码是FFmpeg解码的固定模板：
avformat_open_input(&format_ctx_, filename, nullptr, nullptr);  // 1.打开文件
avformat_find_stream_info(format_ctx_, nullptr);                  // 2.探测流信息
avcodec_find_decoder(codec_params->codec_id);                     // 3.找解码器
avcodec_alloc_context3(codec);                                     // 4.创建上下文
avcodec_parameters_to_context(codec_ctx_, codec_params);           // 5.复制参数
avcodec_open2(codec_ctx_, codec, nullptr);                         // 6.打开解码器
```

**第二步**：阅读 `decodeVideoFrame()` 函数

```cpp
// 解码循环的核心：
while (av_read_frame(format_ctx_, packet) >= 0) {           // 读取压缩包
    if (packet->stream_index == video_stream_index_) {       // 过滤视频流
        avcodec_send_packet(codec_ctx_, packet);              // 发送给解码器
        avcodec_receive_frame(codec_ctx_, frame);             // 获取解码帧
    }
}
```

**第三步**：理解色彩空间转换

```cpp
// 视频解码后是YUV格式，OpenGL需要RGB
sws_scale(sws_ctx_,           // 转换上下文
    frame->data,              // 源数据（YUV）
    frame->linesize,          // 源行跨度
    0, frame->height,         // 起始行和行数
    rgb_data, rgb_linesize);  // 目标数据（RGB）
```

### 2.3 关键知识点

- **时间基(Time Base)**：FFmpeg用分数表示时间单位，`av_q2d()` 转为秒
- **seek操作**：必须seek到关键帧（`AVSEEK_FLAG_BACKWARD`），然后继续解码
- **刷新缓冲区**：seek后必须 `avcodec_flush_buffers()` 清除旧帧
- **文件结束**：发送 `nullptr` packet可刷新解码器中的剩余帧

---

## 3. OpenGL视频渲染

### 3.1 渲染管线

```
顶点数据 → 顶点着色器 → 光栅化 → 片段着色器 → 帧缓冲
  (VBO)    (变换位置)    (生成像素) (计算颜色)   (显示)
```

### 3.2 代码学习路线

**第一步**：理解 `VideoRenderer.cpp` 的 `createGeometry()`

```cpp
// 全屏四边形：4个顶点覆盖整个屏幕
// NDC坐标范围：[-1, 1]
float vertices[] = {
    // 位置(x,y,z)     // 纹理坐标(s,t)
     1.0f,  1.0f, 0,   1.0f, 0.0f,  // 右上
     1.0f, -1.0f, 0,   1.0f, 1.0f,  // 右下
    -1.0f, -1.0f, 0,   0.0f, 1.0f,  // 左下
    -1.0f,  1.0f, 0,   0.0f, 0.0f,  // 左上
};
```

**第二步**：理解纹理上传

```cpp
// 每帧视频数据上传到GPU纹理
glTexSubImage2D(GL_TEXTURE_2D, 0, 0, 0, width, height,
    GL_RGB, GL_UNSIGNED_BYTE, data);
```

**第三步**：学习片段着色器特效

打开 `VideoRenderer.cpp` 中的 `fragment_shader_source`，逐个特效阅读：

```glsl
// 灰度效果 - 最简单的特效
float gray = dot(color.rgb, vec3(0.299, 0.587, 0.114));
// 为什么是这三个数？这是人眼对红绿蓝的感知权重
// 人眼对绿色最敏感(0.587)，红色次之(0.299)，蓝色最弱(0.114)

// 边缘检测 - Sobel算子
// 计算水平和垂直方向的亮度梯度
// 梯度大的地方就是边缘
float gx = -tl - 2.0*l - bl + tr + 2.0*r + br;  // 水平梯度
float gy = -tl - 2.0*t - tr + bl + 2.0*b + br;  // 垂直梯度
float edge = sqrt(gx*gx + gy*gy);                  // 梯度幅值
```

### 3.3 关键OpenGL概念

| 概念 | 作用 | 代码中对应 |
|------|------|-----------|
| VAO | 存储顶点属性配置 | `glGenVertexArrays` |
| VBO | 存储顶点数据 | `glGenBuffers` |
| Shader | GPU程序 | `glCreateShader` |
| Uniform | CPU→GPU参数传递 | `glUniform1i/f` |
| Texture | 图像数据 | `glGenTextures` |

---

## 4. 音频处理与可视化

### 4.1 FFT算法学习

**阅读文件**：`AudioVisualizer.cpp` 的 `fft()` 函数

FFT将时域信号（振幅随时间变化）转换为频域信号（振幅随频率变化）：

```
时域：          频域：
  /\  /\        |
 /  \/  \  →   |  |     |
/        \      |__|_____|___
 时间轴          频率轴
```

**关键步骤**：

1. **位反转排列**：重排输入数据，为分治做准备
2. **蝶形运算**：合并两个N/2点DFT为N点DFT
3. **旋转因子**：`W_N^k = e^(-2πik/N)`，复数运算

### 4.2 窗函数

```cpp
// 汉宁窗：使信号两端平滑衰减到0
window_[i] = 0.5 * (1.0 - cos(2.0 * M_PI * i / (FFT_SIZE - 1)));
```

**为什么需要窗函数？**
- FFT假设输入信号是无限周期的
- 实际只取有限长度，直接截断会产生频谱泄漏
- 窗函数使两端平滑衰减，减少泄漏

### 4.3 频谱柱状图

```cpp
// 将FFT结果分组为bar_count_个频段
// 使用对数缩放：低频段更精细（人耳对低频更敏感）
// dB归一化：20*log10(magnitude)，映射[-60,0]dB → [0,1]
```

---

## 5. MySQL数据库集成

### 5.1 连接流程

**阅读文件**：`MediaDatabase.cpp` 的 `connect()` 函数

```cpp
// 三步连接：
driver_ = sql::mysql::get_mysql_driver_instance();  // 1.获取驱动
connection_ = driver_->connect(url, user, password);  // 2.建立连接
connection_->setSchema(database);                      // 3.选择数据库
```

### 5.2 PreparedStatement防SQL注入

```cpp
// ❌ 危险写法（SQL注入风险）：
stmt->execute("SELECT * FROM media WHERE name = '" + name + "'");

// ✅ 安全写法（参数化查询）：
pstmt = connection_->prepareStatement("SELECT * FROM media WHERE name = ?");
pstmt->setString(1, name);  // 参数自动转义
```

### 5.3 数据库初始化

```bash
# 命令行执行
mysql -u root -p < sql/init_database.sql
```

### 5.4 在代码中启用数据库

编辑 `main.cpp`，取消以下行的注释：

```cpp
app.connectDatabase("127.0.0.1", "root", "你的密码", "media_center");
```

---

## 6. Qt GUI与多线程

### 6.1 为什么需要多线程

视频解码是**CPU密集型**任务，如果在 Qt 主线程（UI线程）中直接解码，界面会卡死。因此必须将解码放到独立线程。

### 6.2 实际线程模型（Qt 版本）

```
Qt 主线程                              播放线程 (playbackThread)
│                                      │
├─ QApplication 事件循环               ├─ av_read_frame() → 读取 AVPacket
├─ 处理鼠标/键盘/菜单事件              ├─ avcodec_send_packet() → 发送给解码器
├─ paintGL() → 渲染 OpenGL 帧          ├─ avcodec_receive_frame() → 获取 AVFrame
├─ update() 触发重绘                   ├─ 如果是音频帧：
│                                      │     swr_convert() → 重采样
│                                      │     atempo filter → 变速（可选）
│                                      │     audio_output_->playPCM() → WASAPI
│                                      │     推送 spectrum 给 AudioVisualizer
│                                      ├─ 如果是视频帧：
│                                      │     sws_scale() → YUV→RGB
│                                      │     video_widget_->setVideoFrame() → 存入缓冲
│                                      └─ 根据 pts 计算 sleep 时间 → 帧率控制
```

### 6.3 线程安全的 UI 更新

**Qt 规定：只能在主线程操作 GUI 控件。**

跨线程更新 UI 的正确方式：

```cpp
// ❌ 错误：直接在子线程修改按钮文字
btn_play_->setText("⏸");  // 可能崩溃！

// ✅ 正确：使用 QMetaObject::invokeMethod 回到主线程
QMetaObject::invokeMethod(this, [this]() {
    btn_play_->setText("⏸");
    statusBar()->showMessage("正在播放: xxx");
}, Qt::QueuedConnection);
```

### 6.4 生命周期互斥 —— 为什么用递归锁

`PlayerController` 的三个方法都需要加锁：

```cpp
bool PlayerController::loadFile(const std::string& filename) {
    std::lock_guard<std::recursive_mutex> lock(lifecycle_mutex_);
    stop();  // stop() 内部也要加同一把锁！
    // ... 打开新文件
}
```

如果用 `std::mutex`，`loadFile()` 持有锁后调用 `stop()`，`stop()` 再尝试加同一把锁 → **死锁**。

`std::recursive_mutex` 允许**同一线程多次加锁**，完美解决此问题。

### 6.5 三道关卡防止并发冲突

| 关卡 | 位置 | 作用 | 机制 |
|------|------|------|------|
| 第1道 | `MainWindow::requestLoadAndPlay()` | 防止 UI 层快速切换产生并发加载请求 | `std::atomic<int> load_request_id_` |
| 第2道 | `PlayerController::loadFile/play/stop` | 防止生命周期方法被多线程同时调用 | `std::recursive_mutex lifecycle_mutex_` |
| 第3道 | `PlayerController::playbackThread()` | 防止旧线程在新文件加载后继续运行 | `std::atomic<int> session_id_` |

### 6.6 同步机制对比

| 机制 | 适用场景 | 代码中示例 | 注意事项 |
|------|---------|-----------|---------|
| `std::atomic<bool/int>` | 简单标志位，无需复杂同步 | `playing_`, `paused_`, `session_id_` | 适合读多写少，无需锁开销 |
| `std::mutex` | 临界区保护 | `seek_mutex_`, `load_mutex_` | 注意死锁，尽量缩小锁范围 |
| `std::recursive_mutex` | 同线程需要重复加锁 | `lifecycle_mutex_` | 性能略低于普通 mutex，仅在必须时使用 |
| `std::condition_variable` | 生产者-消费者等待通知 | `FrameQueue` 中的 `not_empty_` / `not_full_` | 必须与 `std::unique_lock` 配合使用 |
| `QMetaObject::invokeMethod` | 跨线程更新 Qt GUI | `MainWindow` 中的状态栏/按钮更新 | 必须指定 `Qt::QueuedConnection` |

---

## 7. 媒体转码

### 7.1 转码流程

```
源文件 → 解封装 → 解码 → 原始帧 → 缩放/重采样 → 编码 → 封装 → 目标文件
```

### 7.2 使用示例

```cpp
MediaTranscoder transcoder;
transcoder.transcode(
    "input.avi",           // 输入文件
    "output.mp4",          // 输出文件
    1920, 1080,            // 目标分辨率
    "libx264",             // 视频编码器
    "aac",                 // 音频编码器
    4000000,               // 目标码率 4Mbps
    [](float progress) {   // 进度回调
        printf("进度: %.1f%%\n", progress * 100);
    }
);
```

### 7.3 编码参数说明

- **preset**：编码速度 vs 压缩率权衡（ultrafast→slow）
- **CRF**：恒定质量因子（0=无损，18=高质量，28=中等，51=最差）
- **码率**：直接控制输出文件大小

---

## 8. DLL部署与编译排错

### 8.1 编译错误

| 错误信息 | 原因 | 解决方法 |
|---------|------|---------|
| `无法打开 avcodec.h` | FFmpeg头文件路径未配置 | 检查 `vcxproj` → `AdditionalIncludeDirectories` |
| `无法解析的外部符号 avformat_open_input` | FFmpeg lib 未链接 | 检查 `AdditionalDependencies` 添加 `avcodec.lib;avformat.lib;...` |
| `glewInit失败` | GLEW DLL 不在 PATH | 复制 `glew32.dll` 到 exe 目录 |
| Qt 相关编译错误 | Qt 版本/路径不匹配 | 确认 Qt 6.11 MSVC2022 64-bit，检查 `QTDIR` 环境变量 |

### 8.2 运行时 DLL 清单

运行前确保以下 DLL 在 exe 目录或 PATH 中：

```
avcodec.dll, avformat.dll, avutil.dll, swscale.dll, swresample.dll, avfilter.dll  ← FFmpeg
glew32.dll                                                                       ← GLEW
Qt6Core.dll, Qt6Gui.dll, Qt6Widgets.dll, Qt6OpenGLWidgets.dll                    ← Qt 6
```

---

## 9. 问题排查与调试思路（实战案例）

本节记录项目中真实遇到过的 Bug 及其排查思路，帮助你建立音视频调试的直觉。

### 9.1 播放倍速后音频静音

**排查步骤**：
1. 先确认 `atempo` filter 是否被正确创建：`avfilter_graph_create_filter()` 返回值检查。
2. 检查 filter graph 的输入格式是否与 `SwrContext` 的输出格式一致（采样率、声道布局、采样格式）。
3. 用 `av_log_set_level(AV_LOG_DEBUG)` 查看 FFmpeg 内部日志，观察 filter 是否报错。
4. 在 `1.0x` 时 bypass filter，确认原路径是否正常；若正常则说明问题在 filter 链路。

**学到的知识**：`atempo` 只接受 `AV_SAMPLE_FMT_FLT` 或 `AV_SAMPLE_FMT_FLTP`，如果输入 `S16` 会导致格式不匹配。

### 9.2 切换视频后播放卡顿（1-2 fps）

**排查步骤**：
1. **定位卡顿层级**：是解码慢？还是渲染慢？在 `playbackThread` 中加入计时日志。
2. **检查并发**：在 `loadFile` 入口处打印线程 ID，确认是否有多个线程同时调用。
3. **检查旧线程是否退出**：`stop()` 是否真正 `join()` 了旧线程？`session_id_` 是否生效？
4. **检查音频端点状态**：`AudioOutput` 是否残留了旧文件的采样率/声道数？
5. **尝试控制变量**：先禁用音频（注释掉 `audio_output_->playPCM()`），若只播视频不卡，则问题在音频链路。

**学到的知识**：WASAPI 共享模式下的 `IAudioClient` 对格式变化敏感，切换文件时可能需要重新初始化。

### 9.3 必须手动 pause/play 才能恢复

**排查步骤**：
1. 对比 "正常播放" 和 "卡顿播放" 时的 `PlayerController` 状态变量（`playing_`, `paused_`, `stop_requested_`）。
2. 检查 `playbackThread` 的 `while` 循环条件是否意外提前退出或进入死循环。
3. 在 `pause()` 和 `play()` 中加入状态打印，观察状态机是否进入异常状态。
4. 考虑时间戳问题：新文件的 `pts` 基准是否从 0 开始？如果 `pts` 跳变，帧率控制 sleep 可能计算为负数或极大值。

**学到的知识**：FFmpeg 不同文件的 `pts` 不一定从 0 开始，必须用 `av_q2d(time_base)` 正确转换。

### 9.4 调试技巧速查

| 技巧 | 操作 | 适用场景 |
|------|------|---------|
| FFmpeg 详细日志 | `av_log_set_level(AV_LOG_DEBUG)` | 解码器/filter 初始化失败 |
| 播放线程 FPS 计数 | 每 1 秒打印已解码帧数 | 量化卡顿程度，区分解码/渲染瓶颈 |
| 状态机打印 | 在 `play/pause/stop/loadFile` 中输出 `playing_/paused_/session_id_` | 状态机异常 |
| 音频隔离测试 | 注释掉 `playPCM()` 调用 | 确认问题是否由音频引起 |
| OpenGL 错误检查 | 关键 GL 调用后加 `glGetError()` | 纹理上传失败、着色器编译失败 |
| Qt 信号槽日志 | 重写 `event()` 或连接 `qDebug()` | UI 事件丢失或重复触发 |

---

## 10. 核心概念速查表

### 10.1 FFmpeg 核心结构体

| 结构体 | 作用 | 生命周期 | 本项目中的位置 |
|--------|------|---------|--------------|
| `AVFormatContext` | 文件格式上下文，管理所有流 | `open()` 创建，`close()` 释放 | `MediaDecoder::format_ctx_` |
| `AVCodecContext` | 编解码器上下文，保存解码状态 | `open()` 创建，`close()` 释放 | `MediaDecoder::video_codec_ctx_`, `audio_codec_ctx_` |
| `AVPacket` | 压缩数据包（从文件读取的原始数据） | `av_packet_alloc()` / `av_packet_unref()` | `MediaDecoder::decodeNextFrame()` |
| `AVFrame` | 解码后的原始帧（YUV / PCM） | `av_frame_alloc()` / `av_frame_free()` | 每次解码后临时创建 |
| `SwsContext` | 视频色彩空间转换上下文 | `sws_getContext()` / `sws_freeContext()` | `MediaDecoder::sws_ctx_` |
| `SwrContext` | 音频重采样上下文 | `swr_alloc_set_opts()` / `swr_free()` | `MediaDecoder::swr_ctx_` |
| `AVFilterGraph` | 音频滤波图（`atempo` 链路） | `avfilter_graph_alloc()` / `avfilter_graph_free()` | `MediaDecoder::filter_graph_` |

### 10.2 Qt 核心类

| 类 | 作用 | 本项目用法 |
|---|------|-----------|
| `QApplication` | Qt 应用程序入口，事件循环 | `main.cpp` 中创建 |
| `QMainWindow` | 主窗口，含菜单栏/工具栏/状态栏/停靠窗口 | `MainWindow` 继承 |
| `QOpenGLWidget` | 带 OpenGL 上下文的 QWidget | `VideoWidget` 继承，用于渲染视频 |
| `QTimer` | 定时器，触发周期性事件 | `VideoWidget` 中 16ms 触发 `update()` 实现 60fps |
| `QMetaObject::invokeMethod` | 跨线程调用槽函数 | `MainWindow` 中子线程更新 UI |
| `QThread` / `std::thread` | 多线程 | `MainWindow` 用 `std::thread(detach)` 做异步加载 |

### 10.3 OpenGL 核心对象

| 对象 | 作用 | 创建/销毁函数 |
|------|------|-------------|
| VAO | 存储顶点属性配置 | `glGenVertexArrays` / `glDeleteVertexArrays` |
| VBO | 存储顶点数据 | `glGenBuffers` / `glDeleteBuffers` |
| EBO | 存储索引数据 | `glGenBuffers` / `glDeleteBuffers` |
| Shader | GPU 程序 | `glCreateShader` / `glDeleteShader` |
| Program | 着色器程序（顶点+片段链接） | `glCreateProgram` / `glDeleteProgram` |
| Texture | 图像/视频帧数据 | `glGenTextures` / `glDeleteTextures` |
| FBO | 帧缓冲对象（离屏渲染） | `glGenFramebuffers` / `glDeleteFramebuffers` |

### 10.4 关键设计模式

| 模式 | 本项目应用 | 解决的问题 |
|------|-----------|-----------|
| 生产者-消费者 | `FrameQueue` | 解码线程与渲染线程速度不匹配 |
| 单例 | `sql::mysql::get_mysql_driver_instance()` | MySQL 驱动全局唯一 |
| 状态机 | `PlayerController` (playing/paused/stopped) | 播放生命周期管理 |
| 命令队列 | `MainWindow::requestLoadAndPlay()` | 异步操作序列化、去重 |
| 观察者 | Qt 信号槽机制 | UI 与逻辑解耦 |
