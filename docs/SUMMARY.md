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

### 1.2 线程模型（第三步多线程重构）

| 线程 | 职责 | 关键组件 |
|------|------|---------|
| Qt主线程 | GUI渲染、OpenGL绘制、用户输入 | QApplication事件循环 |
| demux/control 线程 | 读包分发、seek/变速处理、队列流控 | PlayerController::playbackThread() 主循环 |
| 音频工作线程 | 解码音频、WASAPI播放、音频主时钟 | std::thread + audio_packets_ + AudioOutput |
| 视频工作线程 | 解码视频、音视频同步、OpenGL渲染 | std::thread + video_packets_ + VideoWidget |
| 异步加载线程 | 文件打开、解码器初始化 | std::thread(detach) |

**关键设计**：
- **三线程播放架构**：demux/control 线程读包分发，音频/视频线程并行解码
- **generation 机制（车次号）**：seek/变速时换车次，旧车次的包被丢弃
- **音频主时钟**：WASAPI 播放进度作为时间基准，视频根据音频时钟同步
- **PacketQueue**：线程安全的 AVPacket 队列，支持阻塞等待和终止标志

## 二、技术栈

| 技术领域 | 具体技术 | 版本/说明 |
|---------|---------|----------|
| 编程语言 | C++17 | `std::atomic`, `std::thread`, `std::make_unique` |
| 音视频编解码 | FFmpeg 8.0 | `avformat`, `avcodec`, `swscale`, `swresample`, `avfilter` (atempo) |
| GUI框架 | Qt 6.11 | `QMainWindow`, `QOpenGLWidget`, `QTimer`, `QMetaObject::invokeMethod` |
| 视频渲染 | OpenGL 3.3 Core Profile | GLSL着色器、VAO/VBO纹理上传 |
| OpenGL加载 | GLEW 2.3.1 | 扩展函数指针管理 |
| 音频播放 | WASAPI (Windows) | `IAudioClient` / `IAudioRenderClient`, 共享模式, 16-bit PCM |
| 数据库 | MariaDB/MySQL 8.0 C API | `mysql_real_connect`, `mysql_query`, `MYSQL*` |
| 构建系统 | CMake + Ninja + MinGW 13.1.0 | x64, C++17标准 |
## 三、核心 Bug 修复与修改记录（完整历史）

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

### 3.3 倍速时播放时间跳动

**症状**：播放速度不为 1.0x 时，时间显示在音频/视频 PTS 之间跳动。

**根因**：
- 播放循环中同时使用音频 PTS 和视频 PTS 更新 `current_time_`，两者在倍速时更新频率不一致，导致时间显示跳动。

**修复内容**：
- **PlayerController.cpp**：优先使用视频时钟更新时间，仅在没有视频时使用音频时钟。
- 代码逻辑：`const bool use_video_clock = decoder_->hasVideo();`，根据此标志选择时间源。

### 3.4 键盘快捷键失效（Space, Up, Down, F）

**症状**：Space（播放/暂停）、Up/Down（音量）、F（收藏）等快捷键无反应。

**根因**：
- UI 控件（如 VideoWidget）抢占焦点，导致主窗口的 `keyPressEvent` 无法接收到键盘事件。
- 原实现依赖 `keyPressEvent`，焦点在子控件时父窗口无法捕获按键。

**修复内容**：
- **MainWindow.cpp**：改用 `QShortcut` 统一处理播放、音量、上一首/下一首、静音和倍速快捷键。
- 设置 `shortcut->setContext(Qt::ApplicationShortcut)` 使快捷键全局生效。
- **VideoWidget.cpp**：设置 `setFocusPolicy(Qt::NoFocus)` 防止 OpenGL 控件抢占焦点。

### 3.5 退出快捷键从 X 改为 Esc

**症状**：用户希望使用 Esc 键退出，而不是 X 键。

**根因**：
- 原实现使用 X 键作为退出快捷键，不符合常见习惯。

 **修复内容**：
 - **MainWindow.cpp**：移除 X 键快捷键，添加 Esc 键快捷键。
 - 更新菜单帮助文本，将 "退出 (X)" 改为 "退出 (Esc)"。
 - 保留 `Esc` 作为兜底退出键，避免在特殊焦点状态下无法关闭窗口。

### 3.6 数据库连接异常（Debug 模式崩溃）

**症状**：Debug 模式下连接 MySQL 数据库时程序崩溃。

**根因**：
- MySQL Connector/C++ 和 FFmpeg 库是 Release 版编译，与 Debug 版本的 STL 运行时二进制不兼容。
- Debug 版本的 `std::string`、`std::vector` 等容器内存布局与 Release 版不同，导致跨 DLL 边界访问时崩溃。

**修复内容**：
- **MediaDatabase.cpp**：改用 `sql::ConnectOptionsMap` 设置连接参数，增强异常处理。
- 添加详细的错误日志输出，便于诊断连接失败原因。
- **建议**：使用 Release 模式运行程序，避免 Debug/Release 二进制不兼容问题。

### 3.7 F 键收藏功能无反应 + 数据库为空

**症状**：
- 按 F 键切换收藏状态无反应。
- 数据库连接成功，但 `media_library` 表为空。

**根因**：
- 缺少数据库自动同步逻辑：加载文件时未自动插入或更新数据库记录。
- 缺少路径查询功能：无法判断文件是否已在数据库中。
- F 键收藏切换未更新数据库和内存状态。

**修复内容**：
- **MediaDatabase.h/cpp**：新增 `getMediaByPath(path, info)` 方法，根据文件路径查询媒体记录。
- **MainWindow.cpp**：在 `requestLoadAndPlay()` 中添加数据库自动同步逻辑：
  - 加载成功后查询文件是否已存在
  - 若存在：同步数据库中的 `db_id`、`is_favorite`、`title`、`tags` 到内存
  - 若不存在：调用 `addMedia()` 插入新记录
  - 调用 `recordPlayHistory()` 记录播放历史
- **MainWindow.cpp**：F 键快捷键连接到收藏切换逻辑：
  - 获取当前媒体信息
  - 切换 `is_favorite` 状态
  - 调用 `database_->updateMedia(info)` 更新数据库
  - 调用 `player_->setMediaInfo(info)` 更新内存状态

### 3.8 IntelliSense 错误

**症状**：MediaDatabase.cpp/h 中出现 IntelliSense 报错，不影响编译但影响开发体验。

**根因**：
- 头文件和源文件中存在重复的函数声明或实现。
- 缺少必要的头文件包含或命名空间声明。

**修复内容**：
- **MediaDatabase.h**：清理重复声明，确保所有方法只声明一次。
- **MediaDatabase.cpp**：移除重复的函数实现，确保每个方法只有一个实现。
- 添加必要的 `#include` 指令和命名空间声明。

### 3.9 转码工具崩溃与时长异常

**症状**：
- 设置好输入输出后，在 `MediaTranscoder.cpp` 中引发异常执行中断。
- 转码生成后的文件时长不对（如 5s 变 47 分钟），画面声音异常。
- AVI 转回 MP4 时没有画面。

### 3.10 频谱可视化与高 DPI 渲染修复

**症状**：
- **画面偏移**：在迁移至 MinGW/CMake 构建后，视频和频谱画面只占据窗口左下角，未能铺满。
- **视觉失衡**：频谱图左侧（低频）高度远高于右侧（高频），右侧几乎不跳动。

**根因**：
- **高 DPI 缩放**：`QOpenGLWidget` 在高分屏下，`width()` 返回的是逻辑坐标，而 OpenGL 的 `glViewport` 需要物理像素。如果不乘以 `devicePixelRatioF()`，视口只会覆盖真实帧缓冲的左下角。
- **能量分布与分组逻辑**：
  - **物理特性**：自然音频能量符合粉红噪声规律，低频能量远大于高频。也就是说，频率越低，通常能量越高
  - **线性分组**：代码采用 `usable_bins / bar_count_` 线性平分频率，导致高频能量被过度稀释，不符合人耳的对数感知。

**修复内容**：
- **VideoWidget.cpp**：修改 `paintGL()` 渲染逻辑：
  - 引入 `devicePixelRatioF()` 获取缩放因子。
  - 计算物理像素尺寸 `viewport_width = width() * dpr`。
  - 将物理像素尺寸传递给 `renderer_->setViewport()` 和 `visualizer_->render()`。
- **AudioVisualizer.cpp**：优化代码可读性，明确了 `computeSpectrum()` 与 `setSpectrumData()` 的冗余关系（目前核心路径走 `computeSpectrum`）。

**状态**：
- **已修复**：画面已正确铺满窗口。
- **保留现状**：频谱“左高右低”现象确认为物理特性与线性分组导致，作为 V1 版本的技术底座保留，暂不进行对数分组重构，优先进入 V2 AI 增强阶段。

**根因**：
- **音频 Planar 格式处理错误**：原代码错误地使用了 `av_samples_alloc` 和 `memcpy` 处理平面音频（如 FLTP），导致内存越界。
- **时间戳 (PTS) 未缩放**：直接拷贝解码帧的 PTS，未根据输入输出流的 `time_base` 进行 `av_rescale_q` 转换。
- **MP4 兼容性与上下文初始化**：MP4 对像素格式要求严格（需 YUV420P），且 AVI 头部信息不全导致 `sws_ctx` 过早初始化失败。

**修复内容**：
1. **音频重采样重构**：移除中间缓冲区，直接重采样到目标 `AVFrame` 的缓冲区，自动处理平面格式分配。
2. **PTS 精确转换**：使用 `av_rescale_q` 在输入时间基和编码器时间基之间换算时间戳。
3. **延迟初始化转换器**：在处理第一帧数据时根据实际格式动态创建 `sws_ctx` 和 `swr_ctx`。
4. **提升兼容性**：强制视频编码器输出 `yuv420p` 格式，并增加解码器/编码器 Flush 逻辑，确保所有帧被写入。

### 3.10 静音切换功能缺失

**症状**：按 M 键无法实现静音/解除静音。

**修复内容**：
- **MainWindow.h/cpp**：新增 `toggleMute()` 槽函数，使用 `last_volume_` 保存静音前音量。
- **快捷键绑定**：在 `setupShortcuts` 中添加 `Qt::Key_M` 绑定，实现一键静音恢复及状态栏提示。

### 3.11 项目代码清理与优化

**操作**：
- **移除过时组件**：删除了已废弃的 `Application.cpp/h` (GLFW框架) 和 `FrameQueue.h` (早期设计)。
- **同步工程配置**：清理了 `.vcxproj` 和 `.filters` 中的失效引用。
- **修复逻辑中断**：修复了 `updatePlaybackInfo` 中因编辑错误导致的变量未定义和语法崩溃。

### 3.12 第三步多线程播放架构重构

**背景**：为提升播放性能和音视频同步稳定性，将单线程播放循环重构为三线程架构。

**改动内容**：

#### A. 新增 PacketQueue 线程安全队列

**文件**：`PacketQueue.h` / `PacketQueue.cpp`

- 新增线程安全的 AVPacket 队列
- 支持 `push()`/`pop()` 阻塞等待
- 支持 `abort()`/`finish()` 终止标志
- 支持队列流控（超过 80 个包时休眠）

#### B. MediaDecoder 接口拆分

**文件**：`MediaDecoder.h` / `MediaDecoder.cpp`

- 新增分离接口：
  - `readPacket()` - 读取压缩包
  - `isAudioPacket()` / `isVideoPacket()` - 判断包类型
  - `decodeAudioPacket()` - 解码音频包
  - `decodeVideoPacket()` - 解码视频包
  - `flushAudioDecoder()` / `flushVideoDecoder()` - 刷新解码器
  - `receiveAudioFrames()` / `receiveVideoFrames()` - 接收解码帧
- `current_time_` 改为原子变量
- 保留 `decodeNextFrame()` 作为遗留接口（不再使用）

#### C. PlayerController 三线程架构

**文件**：`PlayerController.h` / `PlayerController.cpp`

- 新增 `audio_packets_` 和 `video_packets_` 队列成员
- 重构 `playbackThread()` 为 demux/control 主循环：
  - 读取压缩包并分发到对应队列
  - seek 处理：清空队列 + `decoder_->seek()`
  - 变速处理：换车次 + 重置音频时钟 + `setPlaybackSpeed()`
  - 队列流控：超过 80 个包时休眠
- 新增音频工作线程：
  - 从 `audio_packets_` 取包
  - `decodeAudioPacket()` 解码
  - 音量调整 → `audio_output_->play()` 播放
  - 更新音频主时钟
  - generation 检查
- 新增视频工作线程：
  - 从 `video_packets_` 取包
  - `decodeVideoPacket()` 解码
  - 根据音频主时钟等待/丢帧
  - 纯音频文件用墙时钟同步
  - generation 检查

#### D. generation 机制（车次号）

**设计**：
- `stream_generation` 原子计数器
- seek/变速时 `fetch_add(1)` 换车次
- 队列里的旧车次包被丢弃
- 避免旧速度的包用新速度解码

**类比**：generation = 车次号，packet/frame = 乘客，音频/视频线程 = 接站的人

#### E. 音频主时钟

**设计**：
- WASAPI 播放进度作为时间基准
- `audio_clock_base + playedSeconds * active_speed`
- 视频帧根据音频主时钟等待/丢帧
- 纯音频文件用墙时钟同步

#### F. AudioOutput 扩展

**文件**：`AudioOutput.h` / `AudioOutput.cpp`

- 新增 `getPlayedSeconds()` - 获取已播放秒数
- 新增 `getQueuedSeconds()` - 获取队列秒数
- 新增 `reset()` - 重置缓冲区

#### G. CMake 构建更新

**文件**：`CMakeLists.txt`

- 添加 `PacketQueue.h` 和 `PacketQueue.cpp` 到编译列表

**效果**：
- ✅ 多核并行解码，性能提升
- ✅ 音频主时钟，音视频同步更稳定
- ✅ generation 机制，seek/变速更可靠
- ⚠️ 变速时可能有轻微快进感（WASAPI 缓冲区清空导致）

### 3.13 数据库从 MySQL Connector/C++ 迁移到 MariaDB C API

**背景**：CMake/MinGW 构建下，MySQL Connector/C++ 二进制不兼容问题难以解决。

**改动内容**：

**文件**：`MediaDatabase.h` / `MediaDatabase.cpp`

- 从 `sql::Connection` / `sql::PreparedStatement` 迁移到 `mysql.h` / `MYSQL*`
- 从 C++ API 迁移到 C API
- 使用项目本地便携 MariaDB Connector（`third_party/mariadb/mingw64`）
- CMake 设置 `MEDIASTUDIO_ENABLE_MYSQL=ON` 和 `MEDIASTUDIO_MYSQL_CONNECTOR_ROOT`

**效果**：
- ✅ MinGW 构建下数据库功能正常工作
- ✅ Release 和 Debug 构建都通过
- ✅ 依赖包精简到 ~20MB

### 3.14 当前已知问题与下一步方向

| 问题 | 状态 | 下一步方向 |
|------|------|-----------|
| 倍速音频失音 | **已修复** | 持续测试 0.25x~4.0x 全范围 |
| 倍速时间跳动 | **已修复** | 视频时钟优先逻辑已稳定 |
| 转码器崩溃/异常 | **已修复** | 验证不同封装格式间的互转兼容性 |
| 快捷键失效 | **已修复** | 全局上下文生效 |
| 静音切换 | **已修复** | 状态栏及 UI 同步正常 |
| 切换视频偶发卡顿 | **缓解，未根除** | 排查 `AudioOutput` 残留缓冲区、WASAPI 重新初始化 |

## 四、代码量统计与项目规模

| 统计口径 | 数值 | 备注 |
|---------|------|------|
| 总代码行数 | **4,720 行** | 不含注释/空行，排除 `glad.c` |
| 核心模块 | ~3,200 行 | Decoder, Controller, Renderer, Queue |
| UI 与业务 | ~1,500 行 | MainWindow, Database, Transcoder |

## 五、文件清单与阅读顺序建议

```
核心播放链路（按调用顺序）：
1. main.cpp              → 程序入口，初始化 QApplication + MainWindow
2. MainWindow.cpp/h      → Qt GUI，事件处理，播放列表，所有用户交互入口
3. PlayerController.cpp/h → 播放控制枢纽：三线程架构、generation 机制、音频主时钟
4. PacketQueue.cpp/h      → 线程安全队列：push/pop/abort/finish
5. MediaDecoder.cpp/h    → FFmpeg 解码：readPacket/decodeAudioPacket/decodeVideoPacket
6. AudioOutput.cpp/h     → WASAPI 音频输出：init/play/getPlayedSeconds/reset
7. VideoWidget.cpp/h     → QOpenGLWidget：帧缓冲区、定时刷新、clearFrame
8. VideoRenderer.cpp/h   → OpenGL 渲染：着色器编译、纹理上传、7 种特效
9. AudioVisualizer.cpp/h → FFT 频谱可视化：Cooley-Tukey、频谱柱状图

辅助模块：
10. MediaInfo.h           → 所有结构体定义
11. MediaDatabase.cpp/h   → MariaDB/MySQL 媒体库（可选功能）
12. MediaTranscoder.cpp/h → FFmpeg 格式转码（可选功能）
```

## 六、调试与测试建议

1. **打开 FFmpeg 详细日志**：在 `MediaDecoder::open()` 中加入 `av_log_set_level(AV_LOG_DEBUG)`，观察解码器初始化是否有异常。
2. **添加播放线程 FPS 日志**：在 `playbackThread` 循环中每隔 1 秒打印已渲染帧数，量化卡顿程度。
3. **检查 AudioOutput 残留**：在 `loadFile` 中 `stop()` 后，确认 `AudioOutput::cleanup()` 是否完整释放了 `IAudioClient` 和缓冲区。
4. **WASAPI 重新初始化**：尝试在每次 `loadFile` 时完整销毁并重建 `AudioOutput`，排除音频端点状态污染。
5. **解码器 flush**：在 `MediaDecoder::seek()` 和 `close()` 中增加 `avcodec_flush_buffers()` 调用，确保旧帧不残留。

## 七、V1 阶段收尾建议

当前项目已经具备 V1 传统播放器的主体功能，但如果要达到更稳定、可展示、可面试讲解的程度，建议继续补强以下内容。

### 1. 补强音视频同步

当前播放节奏主要依赖视频帧 `pts` 和系统时钟控制显示。建议后续升级为更标准的音频主时钟方案：

```text
音频播放时钟
  ↓
计算当前视频帧 pts 与音频时钟差值
  ↓
视频过早：等待显示
视频过晚：立即显示或丢弃过期帧
  ↓
保持音视频同步
```

建议新增或整理以下状态：

- **音频时钟**：根据已写入或已播放 PCM 时长估算。
- **视频时钟**：当前待显示视频帧的 `pts`。
- **同步误差**：`video_clock - audio_clock`。
- **丢帧策略**：视频落后超过阈值时丢弃过期帧。

### 2. 增加实时播放调试信息

建议在播放器状态栏、媒体信息面板或日志中增加：

- **实际渲染 FPS**：不是媒体文件原始帧率，而是播放器实际显示帧率。
- **解码 FPS**：每秒解码出的视频帧数量。
- **A/V diff**：音频时钟和视频时钟的差值。
- **丢帧数量**：用于判断同步和性能问题。
- **当前队列/缓冲状态**：后续拆分音频队列、视频队列时很有用。

这些信息对调试卡顿、seek 异常、音画不同步非常关键，也适合写进 README 和面试项目介绍。

### 3. 优化 OpenGL 视频渲染路径

当前视频路径是 FFmpeg 解码后通过 `sws_scale()` 转为 RGBA，再上传到 OpenGL 纹理渲染。这个方案简单稳定，适合 V1。

后续可以升级为更专业的播放器渲染方式：

```text
FFmpeg 解码输出 YUV420P / NV12
  ↓
OpenGL 上传 Y / U / V 或 NV12 纹理
  ↓
Shader 中完成 YUV → RGB
  ↓
显示最终画面
```

升级收益：

- **减少 CPU 像素格式转换开销**。
- **更接近真实播放器内核实现**。
- **更方便后续接入 GPU 侧图像处理和 AI 前后处理**。
- **面试时更容易体现 OpenGL 和音视频底层理解**。

### 4. 完善稳定性测试

建议至少测试以下场景：

- **不同封装格式**：MP4、MKV、AVI、MOV、FLV。
- **不同编码格式**：H.264、H.265、MPEG-4、AAC、MP3。
- **不同帧率**：24fps、30fps、60fps。
- **长时间播放**：连续播放 30 分钟以上。
- **频繁操作**：连续 seek、暂停恢复、快速切换文件。
- **异常输入**：无音频视频、纯音频文件、损坏文件、超大分辨率文件。
- **边界情况**：播放到末尾、seek 到开头、seek 到末尾、重复打开同一文件。

每次测试建议记录：

- 是否崩溃。
- 是否卡死。
- 是否音画不同步。
- 是否出现旧帧残留。
- 是否音频设备无法恢复。
- 是否内存持续增长。

### 5. 整理项目展示材料

建议把 V1 阶段整理成可以直接放进简历和 GitHub 的形式：

- **README**：说明项目定位、技术栈、核心功能和运行方式。
- **架构图**：展示 `MainWindow → PlayerController → MediaDecoder / AudioOutput / VideoWidget` 的调用关系。
- **播放流程图**：展示打开文件、解封装、解码、音频输出、视频渲染流程。
- **线程模型图**：说明 Qt 主线程、播放线程、音频输出、OpenGL 渲染之间的关系。
- **演示截图**：展示播放界面、进度条、播放列表、媒体信息、视频特效。
- **演示视频**：录制 1-2 分钟，覆盖打开文件、暂停、seek、倍速、特效、频谱等功能。

完成这些收尾工作后，项目可以更稳妥地视为 **V1 稳定版播放器**。
