# MediaStudio - 音视频工作站 修改总结文档

## 一、项目概述与架构

MediaStudio 是一个基于 C++17 的桌面音视频播放器，当前采用 **Qt 6.11 + OpenGL + FFmpeg + WASAPI** 技术栈。项目经历了从 GLFW 原生窗口到 Qt Widgets 的迁移，并持续迭代修复播放稳定性问题。

### 1.1 系统架构图

```
┌─────────────────────────────────────────────────────────────┐
│                     MainWindow (Qt主窗口)                     │
│  QMainWindow · 菜单栏 · 工具栏 · 停靠窗口 · 状态栏           │
├─────────────┬──────────────┬──────────────┬───────────────┤
│ VideoWidget │PlayerController│ 播放控制UI   │ 播放列表/信息  │
│(QOpenGLWidget)│ (播放控制器)  │ 按钮/滑块/   │ QListWidget   │
│  OpenGL渲染  ├──────────────┤  下拉框     │ QTextEdit     │
├─────────────┤ MediaDecoder  ├──────────────┴───────────────┤
│VideoRenderer│ (FFmpeg解码)  │                              │
│ GLSL特效    ├──────────────┤                              │
├─────────────┤ AudioOutput   │                              │
│AudioVisualiz│ (WASAPI播放)  │                              │
│ FFT+OpenGL  ├──────────────┤                              │
├─────────────┤ MediaDatabase │  MediaTranscoder (格式转码)   │
│             │ (MySQL媒体库)  │  FFmpeg编码API               │
└─────────────┴──────────────┴───────────────────────────────┘
```

### 1.2 线程模型

| 线程 | 职责 | 关键组件 |
|------|------|---------|
| Qt主线程 | GUI渲染、OpenGL绘制、用户输入 | QApplication事件循环 |
| 播放线程 (1个) | 顺序解复用→解码→音频输出→视频帧推送 | PlayerController::playbackThread |
| 异步加载线程 | 文件打开、解码器初始化 | std::thread(detach) |

**关键设计**：单线程播放循环（非多线程解码），用 `session_id_` 原子变量区分新旧播放会话，旧线程检测到会话过期自动退出。

## 二、技术栈

| 技术领域 | 具体技术 | 版本/说明 |
|---------|---------|----------|
| 编程语言 | C++17 | `std::atomic`, `std::thread`, `std::make_unique` |
| 音视频编解码 | FFmpeg 8.0 | `avformat`, `avcodec`, `swscale`, `swresample`, `avfilter` (atempo) |
| GUI框架 | Qt 6.11 | `QMainWindow`, `QOpenGLWidget`, `QTimer`, `QMetaObject::invokeMethod` |
| 视频渲染 | OpenGL 3.3 Core Profile | GLSL着色器、VAO/VBO纹理上传 |
| OpenGL加载 | GLEW 2.3.1 | 扩展函数指针管理 |
| 音频播放 | WASAPI (Windows) | `IAudioClient` / `IAudioRenderClient`, 共享模式, 16-bit PCM |
| 数据库 | MySQL 8.0 + Connector/C++ | `PreparedStatement`, 事务 |
| 构建系统 | VS2022 MSVC v143 | x64, C++17标准 |
## 三、核心 Bug 修复与修改记录

### 3.1 播放倍速 >1.0x 时音频失速/失音

**症状**：视频倍速画面正常，但音频要么变调、要么完全静音。

**根因**：
- 早期实现仅通过缩短视频帧 sleep 时间实现倍速，音频仍以原速播放，导致音视频严重不同步。
- 引入 FFmpeg `atempo` 音频滤波器后，滤波图初始化参数与 `SwrContext` 输出格式（采样率、声道布局）不一致，导致输出全为静音。
- `atempo` 在 1.0x 时仍被激活，增加不必要的计算并可能引入相位误差。

**修复内容**：
1. **MediaDecoder.cpp**：引入 `AVFilterGraph` / `AVFilterContext`，建立 `abuffer` → `atempo` → `abuffersink` 链路。
2. `seek` 或改变倍速时重新初始化滤波图，保证输入格式与 `SwrContext` 输出一致。
3. `setPlaybackSpeed(1.0f)` 时短路 bypass filter，直接走原始 `swr_convert` 重采样路径。
4. 音频解码线程中优先处理音频帧，防止因视频帧阻塞导致音频 starvation。

### 3.2 切换第二个视频后播放卡顿（1-2 fps）

**症状**：第一个视频正常播放；打开第二个视频后，画面卡顿到 1-2 fps，必须手动双击暂停/播放才能恢复。

**根因**：
- `MainWindow` 多处（打开文件、播放列表双击、上一首/下一首）直接调用 `player_->loadFile()`，且使用 `std::thread(...).detach()` 异步加载。
- 快速连续切换会产生**并发 `loadFile` 调用**：旧播放线程未完全退出，新线程已开始初始化 `AVCodecContext`，导致 FFmpeg 内部状态损坏、音频缓冲区残留旧数据。
- `PlayerController::loadFile`、`play`、`stop` 之间缺乏互斥，生命周期方法被多线程同时调用。

**修复内容**：

#### A. MainWindow — 请求序列化与去重

新增 `requestLoadAndPlay(path, display_name)` 方法，集中管理所有文件加载请求：

- 使用 `std::atomic<int> load_request_id_` 为每次请求生成唯一 ID。
- 使用 `std::mutex load_mutex_` 保证同一线程内**串行执行** `loadFile` + `play`。
- 若新请求到达，旧线程在获取锁前检查 ID，若已过期则直接返回，避免竞争。
- UI 更新通过 `QMetaObject::invokeMethod(..., Qt::QueuedConnection)` 回到 Qt 主线程，防止跨线程操作 GUI。

替换点：
- `onOpenFile()`、`onPlaylistDoubleClick()`、`onNextTrack()`、`onPrevTrack()` 全部改为调用 `requestLoadAndPlay()`。

#### B. PlayerController — 生命周期互斥

新增 `std::recursive_mutex lifecycle_mutex_`，保护 `loadFile`、`play`、`stop`：

- `loadFile` 中先加锁，再调用 `stop()`（等待旧线程退出），最后打开新文件。
- 使用**递归锁**是因为 `loadFile` 内部会调用 `stop()`，而 `stop()` 也需要加同一把锁；`std::mutex` 会导致死锁。

#### C. 自动暂停与清帧

在 `requestLoadAndPlay()` 开始处：

1. 若当前正在播放，先调用 `player_->pause()` 并更新 UI 按钮状态。
2. 调用 `video_widget_->clearFrame()` 清空 OpenGL 帧缓冲区，防止旧视频帧残留。

#### D. 自动暂停/播放周期（临时 workaround）

加载成功后，在加锁区域内执行 `play() → pause() → play()`，模拟用户手动双击暂停/播放，帮助稳定播放状态。

**注意**：该 workaround 仍未彻底根除偶发卡顿，需要进一步排查根本原因。

### 3.3 当前已知问题与下一步方向

| 问题 | 状态 | 下一步方向 |
|------|------|-----------|
| 倍速音频失音 | **已修复** | 持续测试 0.25x~4.0x 全范围 |
| 切换视频偶发卡顿 | **缓解，未根除** | 排查 `AudioOutput` 残留缓冲区、`AVCodecContext` flush、WASAPI 重新初始化 |
| 必须手动 pause/play 才能恢复 | **部分自动化** | 研究根本原因：可能是音频输出端未正确 reset，或解码器时间戳基准未重置 |

## 四、关键代码变更位置

| 文件 | 变更行范围 | 变更内容 |
|------|-----------|---------|
| `MainWindow.h` | 48-50, 104-105, 138-140 | 添加 `requestLoadAndPlay`、原子 ID、加载互斥锁声明 |
| `MainWindow.cpp` | 49-71, 483-503, 505-515, 552-581, 677-697, 738-808, 871-884 | 实现异步加载、替换所有直接 `loadFile` 调用、自动 pause+清帧+play/pause 周期 |
| `PlayerController.h` | 125-127 | 添加 `lifecycle_mutex_`（`std::recursive_mutex`）声明 |
| `PlayerController.cpp` | 40-61, 69-90, 105-126 | `loadFile`、`play`、`stop` 加递归锁；`loadFile` 内调用 `stop` 确保旧线程退出 |
| `MediaDecoder.cpp/.h` | 多处 | 集成 `AVFilterGraph` / `atempo`，`seek` 时重新初始化滤波图 |

## 五、文件清单与阅读顺序建议

```
核心播放链路（按调用顺序）：
1. main.cpp              → 程序入口，初始化 QApplication + MainWindow
2. MainWindow.cpp/h      → Qt GUI，事件处理，播放列表，所有用户交互入口
3. PlayerController.cpp/h → 播放控制枢纽：生命周期、播放线程、seek、倍速、音量
4. MediaDecoder.cpp/h    → FFmpeg 解码：open/close/decodeNextFrame/seek/atempo filter
5. AudioOutput.cpp/h     → WASAPI 音频输出：init/playPCM/setVolume/pause/resume
6. VideoWidget.cpp/h     → QOpenGLWidget：帧缓冲区、定时刷新、clearFrame
7. VideoRenderer.cpp/h   → OpenGL 渲染：着色器编译、纹理上传、7 种特效
8. AudioVisualizer.cpp/h → FFT 频谱可视化：Cooley-Tukey、频谱柱状图

辅助模块：
9.  MediaInfo.h           → 所有结构体定义（MediaInfo / VideoStreamInfo / AudioStreamInfo / VideoEffect）
10. FrameQueue.h          → 线程安全模板队列（生产者-消费者），当前播放线程中未直接使用
11. MediaDatabase.cpp/h   → MySQL 媒体库（可选功能）
12. MediaTranscoder.cpp/h → FFmpeg 格式转码（可选功能）
13. Application.cpp/h      → 旧 GLFW 版本主循环（已废弃，供参考）
```

## 六、调试与测试建议

1. **打开 FFmpeg 详细日志**：在 `MediaDecoder::open()` 中加入 `av_log_set_level(AV_LOG_DEBUG)`，观察解码器初始化是否有异常。
2. **添加播放线程 FPS 日志**：在 `playbackThread` 循环中每隔 1 秒打印已渲染帧数，量化卡顿程度。
3. **检查 AudioOutput 残留**：在 `loadFile` 中 `stop()` 后，确认 `AudioOutput::cleanup()` 是否完整释放了 `IAudioClient` 和缓冲区。
4. **WASAPI 重新初始化**：尝试在每次 `loadFile` 时完整销毁并重建 `AudioOutput`，排除音频端点状态污染。
5. **解码器 flush**：在 `MediaDecoder::seek()` 和 `close()` 中增加 `avcodec_flush_buffers()` 调用，确保旧帧不残留。
