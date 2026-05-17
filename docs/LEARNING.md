# MediaStudio 学习使用文档

## 目录

1. [项目整体框架](#1-项目整体框架)
2. [阅读顺序与学习路径](#2-阅读顺序与学习路径)
3. [核心模块深度解析](#3-核心模块深度解析)
4. [FFmpeg音视频处理](#4-ffmpeg音视频处理)
5. [OpenGL视频渲染](#5-opengl视频渲染)
6. [音频处理与可视化](#6-音频处理与可视化)
7. [Qt GUI与多线程](#7-qt-gui与多线程)
8. [MySQL数据库集成](#8-mysql数据库集成)
9. [媒体转码](#9-媒体转码)
10. [问题排查与调试思路](#10-问题排查与调试思路)
11. [核心概念速查表](#11-核心概念速查表)

---

## 1. 项目整体框架

### 1.1 技术栈全景图

```
┌─────────────────────────────────────────────────────────────────┐
│                         应用层 (Application Layer)                 │
│  MainWindow (Qt QMainWindow) - 用户界面、事件处理、播放列表管理      │
├─────────────────────────────────────────────────────────────────┤
│                      控制层 (Controller Layer)                     │
│  PlayerController - 播放状态管理、线程同步、播放列表控制              │
├─────────────────────────────────────────────────────────────────┤
│                      解码层 (Decoder Layer)                        │
│  MediaDecoder (FFmpeg) - 解封装、解码、色彩转换、音频重采样          │
├─────────────────────────────────────────────────────────────────┤
│                      输出层 (Output Layer)                         │
│  VideoWidget (Qt OpenGL) - 视频帧显示、OpenGL 渲染                  │
│  VideoRenderer (OpenGL) - 着色器特效、纹理管理                       │
│  AudioOutput (WASAPI) - 音频播放、音量控制                          │
│  AudioVisualizer (FFT) - 音频频谱可视化                             │
├─────────────────────────────────────────────────────────────────┤
│                      数据层 (Data Layer)                           │
│  MediaDatabase (MySQL) - 媒体库、收藏、播放历史                       │
└─────────────────────────────────────────────────────────────────┘
```

### 1.2 数据流向图

```
用户操作 (点击/键盘/拖拽)
    ↓
MainWindow (Qt 事件)
    ↓
PlayerController (状态管理)
    ↓
    ├─→ MediaDecoder (FFmpeg 解码)
    │       ├─→ av_read_frame() (读取压缩包)
    │       ├─→ avcodec_send/receive() (解码)
    │       ├─→ sws_scale() (YUV→RGB)
    │       └─→ swr_convert() + atempo (音频重采样+变速)
    │
    ├─→ VideoWidget (Qt OpenGL 显示)
    │       └─→ VideoRenderer (OpenGL 渲染)
    │               └─→ glTexSubImage2D() (纹理上传)
    │
    ├─→ AudioOutput (WASAPI 播放)
    │       └─→ IAudioRenderClient (PCM 写入)
    │
    └─→ AudioVisualizer (FFT 可视化)
            └─→ fft() + 汉宁窗 (频谱分析)
```

### 1.3 线程模型

#### 当前架构（第三步多线程重构）

```
┌─────────────────────────────────────────────────────────────┐
│                    Qt 主线程 (GUI Thread)                       │
│  - QApplication 事件循环                                       │
│  - 用户输入处理 (鼠标/键盘/菜单)                               │
│  - VideoWidget::paintGL() (OpenGL 渲染)                        │
│  - UI 控件更新 (进度条/按钮/状态栏)                             │
│  - QMetaObject::invokeMethod (跨线程 UI 更新)                   │
├─────────────────────────────────────────────────────────────┤
│            demux/control 线程 (主线程)                          │
│  - PlayerController::playbackThread() 主循环                   │
│  - decoder_->readPacket() (读取 AVPacket)                      │
│  - 判断音频/视频流，分发到对应队列                               │
│  - seek 处理：清空队列 + decoder_->seek()                      │
│  - 变速处理：等待旧缓冲排空后再切速，再更新 setPlaybackSpeed() │
│  - 队列流控：队列超过 80 个包时休眠                              │
├─────────────────────────────────────────────────────────────┤
│                  音频工作线程 (Audio Worker)                     │
│  - audio_packets_.pop() (从队列取包)                           │
│  - decoder_->decodeAudioPacket() (解码音频)                    │
│  - 音量调整 → audio_output_->play() (WASAPI 播放)               │
│  - 更新音频主时钟 (audio_clock_base + playedSeconds * speed)    │
│  - FFT 频谱可视化 → video_widget_->setSpectrumData()           │
│  - generation 检查：跳过旧车次的帧                              │
├─────────────────────────────────────────────────────────────┤
│                  视频工作线程 (Video Worker)                     │
│  - video_packets_.pop() (从队列取包)                           │
│  - decoder_->decodeVideoPacket() (解码视频)                    │
│  - 音视频同步：根据音频主时钟等待/丢帧                          │
│  - 纯音频文件：根据墙时钟同步                                   │
│  - video_widget_->setVideoFrame() (渲染视频帧)                  │
│  - generation 检查：跳过旧车次的帧                              │
├─────────────────────────────────────────────────────────────┤
│              异步加载线程 (Async Load Thread)                    │
│  - MainWindow::requestLoadAndPlay() 中的 std::thread            │
│  - player_->loadFile() (文件加载)                              │
│  - 数据库同步 (getMediaByPath / addMedia / recordPlayHistory)   │
└─────────────────────────────────────────────────────────────┘
```

#### 关键设计：generation 机制（车次号）

- **类比**：generation = 车次号，packet/frame = 乘客，音频/视频线程 = 接站的人
- **seek/变速时**：`stream_generation.fetch_add(1)` 换车次
- **旧包处理**：队列里的旧车次包被丢弃（`packet_generation != stream_generation`）
- **避免混用**：旧速度的包不会用新速度解码，避免音频忽快忽慢

#### 关键设计：音频主时钟

- **WASAPI 播放进度**：`audio_output_->getPlayedSeconds()` 返回已播放秒数
- **时钟计算**：`audio_clock_base + playedSeconds * active_speed`
- **视频同步**：视频帧根据音频主时钟等待/丢帧
- **纯音频文件**：使用墙时钟同步（`playback_anchor` + `anchor_pts`）

#### 关键设计：播放调试信息

播放器状态栏会显示一组轻量统计：渲染 FPS、解码 FPS、A/V diff、丢帧总数、音频/视频队列和音频缓冲。它们主要用于快速判断是解码、同步还是输出侧出现压力。

#### 与单线程版本对比

| 特性 | 单线程版本 | 多线程版本（第三步） |
|------|----------|-------------------|
| 线程数 | 1 个播放线程 | 3 个线程（demux + 音频 + 视频） |
| 解码方式 | 顺序解码 | 并行解码 |
| 音视频同步 | 视频时钟优先 | 音频主时钟 |
| 队列 | 无 | PacketQueue（线程安全） |
| seek/变速 | 直接调用 | generation 机制 + 队列清空 |
| 性能 | 单核受限 | 多核并行 |

### 1.4 核心依赖库


| 库名                  | 版本          | 用途          | 关键 API                                              |
| ------------------- | ----------- | ----------- | --------------------------------------------------- |
| Qt                  | 6.11        | GUI 框架      | QApplication, QMainWindow, QOpenGLWidget, QShortcut |
| FFmpeg              | 8.0         | 音视频编解码      | avformat, avcodec, swscale, swresample, avfilter    |
| OpenGL              | 3.3 Core    | 图形渲染        | glGenTextures, glTexSubImage2D, glUseProgram        |
| GLEW                | 2.3.1       | OpenGL 扩展加载 | glewInit, glewGetProcAddress                        |
| WASAPI              | Windows API | 音频输出        | IAudioClient, IAudioRenderClient                    |
| MySQL Connector/C++ | 8.0         | 数据库连接       | sql::Connection, sql::PreparedStatement             |


---

## 2. 阅读顺序与学习路径

### 2.1 建议阅读顺序（由浅入深）

阅读代码时，**顺着数据流走**最容易理解：


| 阶段        | 文件                        | 作用            | 需要关注的重点                                                                                              | 技术栈                        |
| --------- | ------------------------- | ------------- | ---------------------------------------------------------------------------------------------------- | -------------------------- |
| **入口**    | `main.cpp`                | 程序入口          | COM 初始化、QApplication 创建、命令行参数处理                                                                      | Qt, COM                    |
| **界面声明**  | `MainWindow.h`            | GUI 声明        | UI 控件、信号槽、核心成员变量、快捷键方法                                                                               | Qt Widgets                 |
| **界面实现**  | `MainWindow.cpp`          | GUI 实现 + 事件处理 | `requestLoadAndPlay()` 异步加载、`setupShortcuts()` 全局快捷键、数据库同步逻辑                                         | Qt, 多线程, MySQL             |
| **控制枢纽**  | `PlayerController.h/.cpp` | 播放控制          | `loadFile()`/`play()`/`stop()` 生命周期、`playbackThread()` 三线程架构、`stream_generation` 机制、音频主时钟 | C++17, std::thread, atomic |
| **队列**     | `PacketQueue.h/.cpp`      | 线程安全队列       | `push()`/`pop()` 阻塞等待、`abort()`/`finish()` 终止标志、线程同步                                          | C++17, std::mutex, condition_variable |
| **解码器**   | `MediaDecoder.h/.cpp`     | FFmpeg 解码     | `readPacket()`/`decodeAudioPacket()`/`decodeVideoPacket()` 分离接口、`setPlaybackSpeed()` atempo 滤镜重建       | FFmpeg, 音视频处理              |
| **音频输出**  | `AudioOutput.h/.cpp`      | WASAPI 音频     | `init()` 六步流程、`play()` 缓冲区写入、`getPlayedSeconds()` 音频主时钟、`reset()` 重置                               | WASAPI, COM                |
| **视频显示**  | `VideoWidget.h/.cpp`      | Qt OpenGL     | `QOpenGLWidget` 三件套、帧缓冲互斥锁、`clearFrame()`、焦点策略 `NoFocus`                                             | Qt OpenGL, 多线程             |
| **视频渲染**  | `VideoRenderer.h/.cpp`    | OpenGL 渲染     | 着色器编译、纹理上传、`glTexSubImage2D`、7 种 GLSL 特效                                                             | OpenGL, GLSL               |
| **音频可视化** | `AudioVisualizer.h/.cpp`  | FFT 频谱        | `fft()` Cooley-Tukey 实现、窗函数、分贝归一化                                                                    | 数字信号处理                     |
| **数据结构**  | `MediaInfo.h`             | 结构定义          | `MediaInfo`、`VideoStreamInfo`、`AudioStreamInfo`、`VideoEffect` 枚举、`db_id`、`is_favorite`               | C++ 结构体                    |
| **数据库**   | `MediaDatabase.h/.cpp`    | MySQL 媒体库     | MariaDB C API、`mysql_real_connect()`、`mysql_query()`、CRUD 操作、播放历史记录                                    | MariaDB/MySQL C API         |
| **转码器**   | `MediaTranscoder.h/.cpp`  | 格式转码          | FFmpeg 编码 API 完整链路、PTS 换算、YUV420P 兼容性                                                                | FFmpeg 编码                  |


### 2.2 架构理解口诀

- **一条主线**：`MainWindow` 收到用户操作 → 调用 `PlayerController` → `MediaDecoder` 解码 → `AudioOutput` 播放音频 + `VideoWidget` 显示视频
- **三个线程**：Qt 主线程（GUI + OpenGL）+ demux/control 线程（读包分发）+ 音频线程（解码播放）+ 视频线程（解码渲染）
- **两个队列**：`audio_packets_`（音频包队列）+ `video_packets_`（视频包队列）
- **generation 机制**：seek/变速时换车次，旧车次的包被丢弃
- **音频主时钟**：WASAPI 播放进度作为时间基准，视频根据音频时钟同步
- **四道关卡**：`load_request_id_`（UI 层去重）→ `lifecycle_mutex_`（控制器层互斥）→ `session_id_`（播放线程层会话隔离）→ `stream_generation`（队列层车次隔离）
- **五个同步**：原子变量（标志位）、互斥锁（临界区）、递归锁（生命周期）、条件变量（队列阻塞）、Qt 队列调用（跨线程 UI 更新）

---

## 3. 核心模块深度解析

### 3.1 MainWindow - 用户界面与事件中心

#### 技术栈

- Qt 6.11 (QMainWindow, QDockWidget, QShortcut, QMetaObject)
- 多线程 (std::thread, std::atomic, std::mutex)
- MySQL Connector/C++ (sql::Connection, sql::PreparedStatement)

#### 核心逻辑

**1. 异步文件加载 (`requestLoadAndPlay`)**

```cpp
// 问题：快速切换文件时，多个 loadFile() 并发调用导致 FFmpeg 状态损坏
// 解决：使用原子 ID + 互斥锁实现请求序列化
void MainWindow::requestLoadAndPlay(const std::string& path, const QString& display_name) {
    // 生成唯一请求 ID
    int request_id = ++load_request_id_;
    
    std::thread([this, path, display_name, request_id]() {
        std::lock_guard<std::mutex> lock(load_mutex_);
        // 检查请求是否过期（如果有新请求到达，旧请求直接返回）
        if (request_id != load_request_id_.load()) return;
        
        // 加载文件
        bool loaded = player_->loadFile(path);
        
        // 数据库同步逻辑
        if (loaded && db_connected_) {
            MediaInfo info = player_->getMediaInfo();
            MediaInfo db_info;
            if (database_->getMediaByPath(path, db_info)) {
                // 已存在：同步数据库中的 ID 和收藏状态
                info.db_id = db_info.db_id;
                info.is_favorite = db_info.is_favorite;
            } else {
                // 不存在：添加到数据库
                int id = database_->addMedia(info);
                info.db_id = id;
            }
            player_->setMediaInfo(info);
            database_->recordPlayHistory(info.db_id);
        }
        
        // 跨线程更新 UI
        QMetaObject::invokeMethod(this, [this]() {
            btn_play_->setText("⏸");
            statusBar()->showMessage("正在播放: xxx");
        }, Qt::QueuedConnection);
    }).detach();
}
```

**2. 全局快捷键 (`setupShortcuts`)**

```cpp
// 问题：UI 控件焦点导致快捷键失效（如 VideoWidget 抢占焦点）
// 解决：使用 QShortcut + ApplicationShortcut 上下文
void MainWindow::setupShortcuts() {
    // Space - 播放/暂停
    QShortcut* shortcutSpace = new QShortcut(QKeySequence(Qt::Key_Space), this);
    shortcutSpace->setContext(Qt::ApplicationShortcut);  // 全局生效
    connect(shortcutSpace, &QShortcut::activated, this, &MainWindow::togglePlayPause);
    
    // Esc - 退出
    QShortcut* shortcutEsc = new QShortcut(QKeySequence(Qt::Key_Escape), this);
    shortcutEsc->setContext(Qt::ApplicationShortcut);
    connect(shortcutEsc, &QShortcut::activated, this, &MainWindow::close);
    
    // F - 收藏切换
    QShortcut* shortcutF = new QShortcut(QKeySequence(Qt::Key_F), this);
    shortcutF->setContext(Qt::ApplicationShortcut);
    connect(shortcutF, &QShortcut::activated, this, [this]() {
        if (db_connected_) {
            MediaInfo info = player_->getMediaInfo();
            info.is_favorite = !info.is_favorite;
            database_->updateMedia(info);
            player_->setMediaInfo(info);
        }
    });
}
```

**3. 焦点策略修复**

```cpp
// VideoWidget.cpp
VideoWidget::VideoWidget(QWidget* parent) : QOpenGLWidget(parent) {
    setFocusPolicy(Qt::NoFocus);  // 关键：不抢占焦点，确保 Esc 等全局快捷键生效
}
```

#### 使用方法

- **打开文件**：菜单 文件 → 打开，或拖拽文件到窗口
- **播放控制**：工具栏按钮或快捷键（Space 播放/暂停、Esc 退出、↑/↓ 音量）
- **数据库连接**：菜单 工具 → 连接数据库，输入 MySQL 连接信息
- **收藏功能**：按 F 键切换收藏状态（需先连接数据库）

### 3.2 PlayerController - 播放状态与线程管理

#### 技术栈

- C++17 (std::thread, std::atomic, std::recursive_mutex, std::condition_variable)
- FFmpeg (通过 MediaDecoder)
- WASAPI (通过 AudioOutput)

#### 核心逻辑

**1. 生命周期互斥 (`lifecycle_mutex`_)**

```cpp
// 问题：loadFile() 内部调用 stop()，两者都需要加锁，std::mutex 会死锁
// 解决：使用 std::recursive_mutex 允许同一线程重复加锁
bool PlayerController::loadFile(const std::string& filename) {
    std::lock_guard<std::recursive_mutex> lock(lifecycle_mutex_);
    stop();  // stop() 内部也要加同一把锁，递归锁允许
    // ... 打开新文件
}

void PlayerController::stop() {
    std::lock_guard<std::recursive_mutex> lock(lifecycle_mutex_);
    // ... 停止逻辑
}
```

**2. 会话隔离 (`session_id_`)**

```cpp
// 问题：旧播放线程在新文件加载后继续运行，导致状态混乱
// 解决：每次 play() 递增 session_id_，旧线程检测到不匹配则退出
void PlayerController::play() {
    session_id_++;  // 新会话
    playing_ = true;
    paused_ = false;
    if (!playback_thread_.joinable()) {
        playback_thread_ = std::thread(&PlayerController::playbackThread, this);
    }
}

void PlayerController::playbackThread() {
    const int my_session = session_id_.load();  // 记录当前会话 ID
    while (!stop_requested_ && session_id_.load() == my_session) {
        // 播放逻辑
    }
    // session_id_ 变化时自动退出
}
```

**3. 视频时钟优先 (倍速时间跳动修复)**

```cpp
// 问题：倍速时音频/视频 PTS 混合更新导致时间显示跳动
// 解决：优先使用视频时钟，无视频时才用音频时钟
const bool use_video_clock = decoder_->hasVideo();

if (use_video_clock && frame.type == DecodedFrame::Type::Video) {
    // 视频帧：直接使用视频 PTS
    current_time_ = frame.pts;
} else if (!use_video_clock && frame.type == DecodedFrame::Type::Audio) {
    // 纯音频：使用音频 PTS
    current_time_ = frame.pts;
}
// 避免：音频/视频 PTS 混合更新导致时间跳动
```

#### 使用方法

- **加载文件**：`loadFile(path)` - 打开媒体文件
- **播放控制**：`play()` / `pause()` / `stop()` / `seek(seconds)`
- **参数调节**：`setVolume(0.0-1.0)` / `setSpeed(0.25-4.0)` / `setEffect(effect)`
- **播放列表**：`setPlaylist(files)` / `nextFile()` / `prevFile()`

### 3.3 MediaDecoder - FFmpeg 解码核心

#### 技术栈

- FFmpeg 8.0 (avformat, avcodec, swscale, swresample, avfilter)
- C++17 (std::unique_ptr, RAII)

#### 核心逻辑

**1. 解码流程**

```cpp
bool MediaDecoder::open(const std::string& filename) {
    // 1. 打开文件
    avformat_open_input(&format_ctx_, filename, nullptr, nullptr);
    // 2. 探测流信息
    avformat_find_stream_info(format_ctx_, nullptr);
    // 3. 查找解码器
    AVCodec* codec = avcodec_find_decoder(codec_params->codec_id);
    // 4. 创建解码器上下文
    video_codec_ctx_ = avcodec_alloc_context3(codec);
    // 5. 复制参数
    avcodec_parameters_to_context(video_codec_ctx_, codec_params);
    // 6. 打开解码器
    avcodec_open2(video_codec_ctx_, codec, nullptr);
    // 7. 初始化转换上下文
    sws_ctx_ = sws_getContext(...);  // 视频色彩转换
    swr_ctx_ = swr_alloc_set_opts(...);  // 音频重采样
}

bool MediaDecoder::decodeVideoPacket(const AVPacket* packet, std::vector<DecodedFrame>& out_frames) {
    // 1. 将压缩包发送给解码器
    avcodec_send_packet(video_codec_ctx_, packet);
    // 2. 拉取当前可用的视频帧
    receiveVideoFrames(out_frames);
    // 4. 转换格式
    if (video) sws_scale(sws_ctx_, ...);  // YUV→RGB
    if (audio) swr_convert(swr_ctx_, ...);  // 重采样 + atempo
}
```

**2. 音频倍速滤波器 (`atempo`)**

```cpp
// 问题：倍速时音频变调或静音
// 解决：使用 FFmpeg atempo 滤波器
bool MediaDecoder::initAudioFilterGraph() {
    // 创建滤波图：abuffer → atempo → abuffersink
    audio_filter_graph_ = avfilter_graph_alloc();
    
    // abuffer (音频源)
    avfilter_graph_create_filter(&audio_buffer_src_ctx_, 
        avfilter_get_by_name("abuffer"), "in", args, nullptr, audio_filter_graph_);
    
    // atempo (变速)
    std::string filter_desc = buildAtempoFilterChain(playback_speed_);
    avfilter_graph_parse_ptr(audio_filter_graph_, filter_desc.c_str(), 
        &inputs, &outputs, nullptr);
    
    // abuffersink (音频接收)
    avfilter_graph_create_filter(&audio_buffer_sink_ctx_,
        avfilter_get_by_name("abuffersink"), "out", nullptr, nullptr, audio_filter_graph_);
    
    avfilter_graph_config(audio_filter_graph_, nullptr);
}

std::string MediaDecoder::buildAtempoFilterChain(float speed) const {
    if (speed == 1.0f) return "";  // 1.0x 时 bypass
    // atempo 只支持 0.5-2.0x，需要级联实现更大倍速
    if (speed > 2.0f) {
        return "atempo=2.0,atempo=" + std::to_string(speed / 2.0f);
    } else if (speed < 0.5f) {
        return "atempo=0.5,atempo=" + std::to_string(speed / 0.5f);
    } else {
        return "atempo=" + std::to_string(speed);
    }
}
```

#### 使用方法

- **打开文件**：`open(path)` - 初始化解码器
- **解码视频包**：`decodeVideoPacket(packet, frames)` - 从视频包获取解码帧
- **解码音频包**：`decodeAudioPacket(packet, frames)` - 从音频包获取解码帧
- **跳转**：`seek(seconds)` - 跳转到指定时间
- **倍速**：`setPlaybackSpeed(speed)` - 设置播放速度
- **获取信息**：`getMediaInfo()` - 获取媒体元数据

### 3.4 MediaDatabase - MySQL 媒体库

#### 技术栈

- MySQL Connector/C++ 8.0
- SQL (PreparedStatement, 事务)

#### 核心逻辑

**1. 连接管理 (Debug/Release 兼容)**

```cpp
// 问题：Debug 模式下连接数据库崩溃（STL 二进制不兼容）
// 解决：使用 ConnectOptionsMap + 建议 Release 模式
bool MediaDatabase::connect(const std::string& host, const std::string& user,
                             const std::string& password, const std::string& database) {
    try {
        driver_ = sql::mysql::get_mysql_driver_instance();
        sql::ConnectOptionsMap connection_options;
        connection_options["hostName"] = host;
        connection_options["userName"] = user;
        connection_options["password"] = password;
        connection_options["schema"] = database;
        connection_options["port"] = 3306;
        connection_options["OPT_RECONNECT"] = true;
        connection_ = driver_->connect(connection_options);
        return connection_ != nullptr;
    } catch (const sql::SQLException& e) {
        last_error_ = std::string("连接失败: ") + e.what();
        return false;
    }
}
```

**2. 路径查询 (`getMediaByPath`)**

```cpp
// 用途：加载文件时检查是否已在数据库中
bool MediaDatabase::getMediaByPath(const std::string& path, MediaInfo& info) {
    sql::PreparedStatement* pstmt = connection_->prepareStatement(
        "SELECT * FROM media_library WHERE file_path = ?"
    );
    pstmt->setString(1, path);
    sql::ResultSet* rs = pstmt->executeQuery();
    bool found = false;
    if (rs->next()) {
        readMediaFromResultSet(rs, info);
        found = true;
    }
    delete rs;
    delete pstmt;
    return found;
}
```

**3. 自动同步逻辑**

```cpp
// MainWindow.cpp 中的调用
if (db_connected_) {
    MediaInfo info = player_->getMediaInfo();
    MediaInfo db_info;
    if (database_->getMediaByPath(path, db_info)) {
        // 已存在：同步数据库状态
        info.db_id = db_info.db_id;
        info.is_favorite = db_info.is_favorite;
        info.title = db_info.title;
        info.tags = db_info.tags;
    } else {
        // 不存在：插入新记录
        int id = database_->addMedia(info);
        info.db_id = id;
    }
    player_->setMediaInfo(info);
    database_->recordPlayHistory(info.db_id);
}
```

#### 使用方法

- **连接数据库**：`connect(host, user, password, database)`
- **添加媒体**：`addMedia(info)` - 插入新记录
- **查询媒体**：`getMediaById(id)` / `getMediaByPath(path)`
- **更新媒体**：`updateMedia(info)` - 更新收藏状态等
- **播放历史**：`recordPlayHistory(media_id)` - 记录播放
- **获取收藏**：`getFavorites()` - 获取收藏列表

---

## 4. FFmpeg音视频处理

### 4.1 核心概念

FFmpeg处理媒体文件的核心数据流：

```
媒体文件 → AVPacket(压缩包) → AVFrame(原始帧) → 像素/音频数据
         解封装(av_read_frame)    解码(avcodec_send/receive)
```

**四个核心结构体**：


| 结构体               | 含义      | 类比    |
| ----------------- | ------- | ----- |
| `AVFormatContext` | 文件格式上下文 | 书的目录  |
| `AVCodecContext`  | 编解码器上下文 | 翻译官   |
| `AVPacket`        | 压缩数据包   | 压缩的包裹 |
| `AVFrame`         | 解码后的帧   | 打开的包裹 |


### 4.2 代码学习路线

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

### 4.3 关键知识点

- **时间基(Time Base)**：FFmpeg用分数表示时间单位，`av_q2d()` 转为秒
- **seek操作**：必须seek到关键帧（`AVSEEK_FLAG_BACKWARD`），然后继续解码
- **刷新缓冲区**：seek后必须 `avcodec_flush_buffers()` 清除旧帧
- **文件结束**：发送 `nullptr` packet可刷新解码器中的剩余帧

---

## 5. OpenGL视频渲染

### 5.1 渲染管线

```
顶点数据 → 顶点着色器 → 光栅化 → 片段着色器 → 帧缓冲
  (VBO)    (变换位置)    (生成像素) (计算颜色)   (显示)
```

### 5.2 代码学习路线

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

### 5.3 关键OpenGL概念


| 概念      | 作用          | 代码中对应               |
| ------- | ----------- | ------------------- |
| VAO     | 存储顶点属性配置    | `glGenVertexArrays` |
| VBO     | 存储顶点数据      | `glGenBuffers`      |
| Shader  | GPU程序       | `glCreateShader`    |
| Uniform | CPU→GPU参数传递 | `glUniform1i/f`     |
| Texture | 图像数据        | `glGenTextures`     |


---

## 6. 音频处理与可视化

### 6.1 FFT算法学习

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

### 6.2 窗函数

```cpp
// 汉宁窗：使信号两端平滑衰减到0
window_[i] = 0.5 * (1.0 - cos(2.0 * M_PI * i / (FFT_SIZE - 1)));
```

**为什么需要窗函数？**

- FFT假设输入信号是无限周期的
- 实际只取有限长度，直接截断会产生频谱泄漏
- 窗函数使两端平滑衰减，减少泄漏

### 6.3 频谱柱状图

```cpp
// 将FFT结果分组为bar_count_个频段
// 使用对数缩放：低频段更精细（人耳对低频更敏感）
// dB归一化：20*log10(magnitude)，映射[-60,0]dB → [0,1]
```

---

## 7. MySQL数据库集成

### 7.1 连接流程

**阅读文件**：`MediaDatabase.cpp` 的 `connect()` 函数

```cpp
// 三步连接：
driver_ = sql::mysql::get_mysql_driver_instance();  // 1.获取驱动
connection_ = driver_->connect(url, user, password);  // 2.建立连接
connection_->setSchema(database);                      // 3.选择数据库
```

### 7.2 PreparedStatement防SQL注入

```cpp
// ❌ 危险写法（SQL注入风险）：
stmt->execute("SELECT * FROM media WHERE name = '" + name + "'");

// ✅ 安全写法（参数化查询）：
pstmt = connection_->prepareStatement("SELECT * FROM media WHERE name = ?");
pstmt->setString(1, name);  // 参数自动转义
```

### 7.3 数据库初始化

```bash
# 命令行执行
mysql -u root -p < sql/init_database.sql
```

### 7.4 在代码中启用数据库

编辑 `main.cpp`，取消以下行的注释：

```cpp
window.connectDatabase("127.0.0.1", "root", "你的密码", "media_center");
```

---

## 8. Qt GUI与多线程

### 8.1 为什么需要多线程

视频解码是**CPU密集型**任务，如果在 Qt 主线程（UI线程）中直接解码，界面会卡死。因此必须将解码放到独立线程。

### 8.2 实际线程模型（Qt 版本）

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

### 8.3 线程安全的 UI 更新

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

### 8.4 生命周期互斥 —— 为什么用递归锁

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

### 8.5 三道关卡防止并发冲突


| 关卡  | 位置                                     | 作用                  | 机制                                      |
| --- | -------------------------------------- | ------------------- | --------------------------------------- |
| 第1道 | `MainWindow::requestLoadAndPlay()`     | 防止 UI 层快速切换产生并发加载请求 | `std::atomic<int> load_request_id_`     |
| 第2道 | `PlayerController::loadFile/play/stop` | 防止生命周期方法被多线程同时调用    | `std::recursive_mutex lifecycle_mutex_` |
| 第3道 | `PlayerController::playbackThread()`   | 防止旧线程在新文件加载后继续运行    | `std::atomic<int> session_id_`          |


### 8.6 同步机制对比


| 机制                          | 适用场景         | 代码中示例                                | 注意事项                        |
| --------------------------- | ------------ | ------------------------------------ | --------------------------- |
| `std::atomic<bool/int>`     | 简单标志位，无需复杂同步 | `playing_`, `paused_`, `session_id_` | 适合读多写少，无需锁开销                |
| `std::mutex`                | 临界区保护        | `seek_mutex_`, `load_mutex_`         | 注意死锁，尽量缩小锁范围                |
| `std::recursive_mutex`      | 同线程需要重复加锁    | `lifecycle_mutex_`                   | 性能略低于普通 mutex，仅在必须时使用       |
| `QMetaObject::invokeMethod` | 跨线程更新 Qt GUI | `MainWindow` 中的状态栏/按钮更新              | 必须指定 `Qt::QueuedConnection` |


---

## 9. 媒体转码

### 9.1 转码流程

```
源文件 → 解封装 → 解码 → 原始帧 → 缩放/重采样 → 编码 → 封装 → 目标文件
```

### 9.2 转码核心优化 (PTS 换算与兼容性)

**1. 时间戳 (PTS) 正确换算**

```cpp
// 核心逻辑：使用 av_rescale_q 进行输入时间基到编码器时间基的转换
out_frame->pts = av_rescale_q(frame->pts, in_time_base, enc_ctx->time_base);
```

**2. MP4/移动端最大兼容性**
强制视频编码器输出 `YUV420P` 格式，并延迟初始化 `sws_ctx` 到接收首帧时，确保转码生成的 MP4 在所有平台均可正常播放。

### 9.3 使用示例

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

### 9.4 编码参数说明

- **preset**：编码速度 vs 压缩率权衡（ultrafast→slow）
- **CRF**：恒定质量因子（0=无损，18=高质量，28=中等，51=最差）
- **码率**：直接控制输出文件大小

---

## 10. 问题排查与调试思路（实战案例）

### 10.1 编译错误


| 错误信息                            | 原因               | 解决方法                                                          |
| ------------------------------- | ---------------- | ------------------------------------------------------------- |
| `无法打开 avcodec.h`                | FFmpeg头文件路径未配置   | 检查 `vcxproj` → `AdditionalIncludeDirectories`                 |
| `无法解析的外部符号 avformat_open_input` | FFmpeg lib 未链接   | 检查 `AdditionalDependencies` 添加 `avcodec.lib;avformat.lib;...` |
| `glewInit失败`                    | GLEW DLL 不在 PATH | 复制 `glew32.dll` 到 exe 目录                                      |
| Qt 相关编译错误                       | Qt 版本/路径不匹配      | 确认 Qt 6.11 MSVC2022 64-bit，检查 `QTDIR` 环境变量                    |


### 10.2 运行时 DLL 清单

运行前确保以下 DLL 在 exe 目录或 PATH 中：

```
avcodec.dll, avformat.dll, avutil.dll, swscale.dll, swresample.dll, avfilter.dll  ← FFmpeg
glew32.dll                                                                       ← GLEW
Qt6Core.dll, Qt6Gui.dll, Qt6Widgets.dll, Qt6OpenGLWidgets.dll                    ← Qt 6
```

---

### 10.3 播放倍速后音频静音

**排查步骤**：

1. 先确认 `atempo` filter 是否被正确创建：`avfilter_graph_create_filter()` 返回值检查。
2. 检查 filter graph 的输入格式是否与 `SwrContext` 的输出格式一致（采样率、声道布局、采样格式）。
3. 用 `av_log_set_level(AV_LOG_DEBUG)` 查看 FFmpeg 内部日志，观察 filter 是否报错。
4. 在 `1.0x` 时 bypass filter，确认原路径是否正常；若正常则说明问题在 filter 链路。

**学到的知识**：`atempo` 只接受 `AV_SAMPLE_FMT_FLT` 或 `AV_SAMPLE_FMT_FLTP`，如果输入 `S16` 会导致格式不匹配。

### 10.4 切换视频后播放卡顿（1-2 fps）

**排查步骤**：

1. **定位卡顿层级**：是解码慢？还是渲染慢？在 `playbackThread` 中加入计时日志。
2. **检查并发**：在 `loadFile` 入口处打印线程 ID，确认是否有多个线程同时调用。
3. **检查旧线程是否退出**：`stop()` 是否真正 `join()` 了旧线程？`session_id_` 是否生效？
4. **检查音频端点状态**：`AudioOutput` 是否残留了旧文件的采样率/声道数？
5. **尝试控制变量**：先禁用音频（注释掉 `audio_output_->playPCM()`），若只播视频不卡，则问题在音频链路。

**学到的知识**：WASAPI 共享模式下的 `IAudioClient` 对格式变化敏感，切换文件时可能需要重新初始化。

### 10.5 必须手动 pause/play 才能恢复

**排查步骤**：

1. 对比 "正常播放" 和 "卡顿播放" 时的 `PlayerController` 状态变量（`playing_`, `paused_`, `stop_requested_`）。
2. 检查 `playbackThread` 的 `while` 循环条件是否意外提前退出或进入死循环。
3. 在 `pause()` 和 `play()` 中加入状态打印，观察状态机是否进入异常状态。
4. 考虑时间戳问题：新文件的 `pts` 基准是否从 0 开始？如果 `pts` 跳变，帧率控制 sleep 可能计算为负数或极大值。

**学到的知识**：FFmpeg 不同文件的 `pts` 不一定从 0 开始，必须用 `av_q2d(time_base)` 正确转换。

### 10.6 调试技巧速查


| 技巧          | 操作                                                              | 适用场景             |
| ----------- | --------------------------------------------------------------- | ---------------- |
| FFmpeg 详细日志 | `av_log_set_level(AV_LOG_DEBUG)`                                | 解码器/filter 初始化失败 |
| 播放线程 FPS 计数 | 每 1 秒打印已解码帧数                                                    | 量化卡顿程度，区分解码/渲染瓶颈 |
| 状态机打印       | 在 `play/pause/stop/loadFile` 中输出 `playing_/paused_/session_id_` | 状态机异常            |
| 音频隔离测试      | 注释掉 `playPCM()` 调用                                              | 确认问题是否由音频引起      |
| OpenGL 错误检查 | 关键 GL 调用后加 `glGetError()`                                       | 纹理上传失败、着色器编译失败   |
| Qt 信号槽日志    | 重写 `event()` 或连接 `qDebug()`                                     | UI 事件丢失或重复触发     |


---

## 11. 核心概念速查表

### 11.1 FFmpeg 核心结构体


| 结构体               | 作用                 | 生命周期                                               | 本项目中的位置                                              |
| ----------------- | ------------------ | -------------------------------------------------- | ---------------------------------------------------- |
| `AVFormatContext` | 文件格式上下文，管理所有流      | `open()` 创建，`close()` 释放                           | `MediaDecoder::format_ctx_`                          |
| `AVCodecContext`  | 编解码器上下文，保存解码状态     | `open()` 创建，`close()` 释放                           | `MediaDecoder::video_codec_ctx_`, `audio_codec_ctx_` |
| `AVPacket`        | 压缩数据包（从文件读取的原始数据）  | `av_packet_alloc()` / `av_packet_unref()`          | `MediaDecoder::readPacket()`                         |
| `AVFrame`         | 解码后的原始帧（YUV / PCM） | `av_frame_alloc()` / `av_frame_free()`             | 每次解码后临时创建                                            |
| `SwsContext`      | 视频色彩空间转换上下文        | `sws_getContext()` / `sws_freeContext()`           | `MediaDecoder::sws_ctx_`                             |
| `SwrContext`      | 音频重采样上下文           | `swr_alloc_set_opts()` / `swr_free()`              | `MediaDecoder::swr_ctx_`                             |
| `AVFilterGraph`   | 音频滤波图（`atempo` 链路） | `avfilter_graph_alloc()` / `avfilter_graph_free()` | `MediaDecoder::filter_graph_`                        |


### 11.2 Qt 核心类


| 类                           | 作用                    | 本项目用法                                       |
| --------------------------- | --------------------- | ------------------------------------------- |
| `QApplication`              | Qt 应用程序入口，事件循环        | `main.cpp` 中创建                              |
| `QMainWindow`               | 主窗口，含菜单栏/工具栏/状态栏/停靠窗口 | `MainWindow` 继承                             |
| `QOpenGLWidget`             | 带 OpenGL 上下文的 QWidget | `VideoWidget` 继承，用于渲染视频                     |
| `QTimer`                    | 定时器，触发周期性事件           | `VideoWidget` 中 16ms 触发 `update()` 实现 60fps |
| `QMetaObject::invokeMethod` | 跨线程调用槽函数              | `MainWindow` 中子线程更新 UI                      |
| `QThread` / `std::thread`   | 多线程                   | `MainWindow` 用 `std::thread(detach)` 做异步加载  |


### 11.3 OpenGL 核心对象


| 对象      | 作用             | 创建/销毁函数                                      |
| ------- | -------------- | -------------------------------------------- |
| VAO     | 存储顶点属性配置       | `glGenVertexArrays` / `glDeleteVertexArrays` |
| VBO     | 存储顶点数据         | `glGenBuffers` / `glDeleteBuffers`           |
| EBO     | 存储索引数据         | `glGenBuffers` / `glDeleteBuffers`           |
| Shader  | GPU 程序         | `glCreateShader` / `glDeleteShader`          |
| Program | 着色器程序（顶点+片段链接） | `glCreateProgram` / `glDeleteProgram`        |
| Texture | 图像/视频帧数据       | `glGenTextures` / `glDeleteTextures`         |
| FBO     | 帧缓冲对象（离屏渲染）    | `glGenFramebuffers` / `glDeleteFramebuffers` |


### 11.4 关键设计模式


| 模式   | 本项目应用                                       | 解决的问题               |
| ---- | ------------------------------------------- | ------------------- |
| 请求去重 | `load_request_id_` + `requestLoadAndPlay()` | 连续切换媒体时避免旧加载结果覆盖新请求 |
| 单例   | `sql::mysql::get_mysql_driver_instance()`   | MySQL 驱动全局唯一        |
| 状态机  | `PlayerController` (playing/paused/stopped) | 播放生命周期管理            |
| 命令队列 | `MainWindow::requestLoadAndPlay()`          | 异步操作序列化、去重          |
| 观察者  | Qt 信号槽机制                                    | UI 与逻辑解耦            |
