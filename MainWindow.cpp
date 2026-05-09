/**
 * @file MainWindow.cpp
 * @brief 主窗口实现 - 完整的Qt桌面媒体播放器界面
 */

#include "MainWindow.h"
#include <QFileDialog>
#include <QInputDialog>
#include <QMessageBox>
#include <QHBoxLayout>
#include <QVBoxLayout>
#include <QSplitter>
#include <QGroupBox>
#include <QFont>
#include <QKeyEvent>
#include <QDragEnterEvent>
#include <QDropEvent>
#include <QMimeData>
#include <QUrl>
#include <QFileInfo>
#include <QToolBar>
#include <QStatusBar>
#include <QAction>
#include <QShortcut>
#include <iostream>
#include <chrono>

// ============================================================
// 构造函数
// ============================================================
MainWindow::MainWindow(QWidget* parent)
    : QMainWindow(parent)
    , current_playlist_index_(0)
    , db_connected_(false)
{
    // 创建核心组件
    player_ = new PlayerController();
    database_ = new MediaDatabase();
    transcoder_ = new MediaTranscoder();

    // 先创建停靠窗口（setupMenuBar需要引用它们）
    dock_playlist_ = new QDockWidget("播放列表", this);
    dock_info_ = new QDockWidget("媒体信息", this);

    // 构建UI
    setupUI();
    applyDarkTheme();
    connectSignals();

    // 启动信息更新定时器（200ms刷新一次播放信息）
    QObject::connect(&info_timer_, &QTimer::timeout, this, &MainWindow::updatePlaybackInfo);
    info_timer_.start(200);

    setWindowTitle("MediaStudio - 音视频工作站");
    resize(1280, 720);
}

MainWindow::~MainWindow()
{
    ++load_request_id_;
    if (transcoding_.load()) {
        transcoder_->cancel();
    }
    if (transcoding_thread_.joinable()) {
        transcoding_thread_.join();
    }
    player_->stop();
    delete player_;
    delete database_;
    delete transcoder_;
}

// ============================================================
// UI构建
// ============================================================
void MainWindow::setupUI()
{
    setupMenuBar();
    setupToolBar();
    setupCentralWidget();
    setupDockWidgets();
    setupBottomBar();
}

void MainWindow::setupMenuBar()
{
    // 文件菜单
    QMenu* menu_file = menuBar()->addMenu("文件");
    menu_file->addAction("打开文件(&O)", QKeySequence(), this, &MainWindow::openFile);
    menu_file->addAction("添加到播放列表", this, &MainWindow::addToPlaylist);
    menu_file->addSeparator();
    menu_file->addAction("退出(Esc)", QKeySequence(Qt::Key_Escape), this, &QWidget::close);

    // 视图菜单
    QMenu* menu_view = menuBar()->addMenu("视图");
    menu_view->addAction(dock_playlist_->toggleViewAction());
    menu_view->addAction(dock_info_->toggleViewAction());
    menu_view->addSeparator();
    menu_view->addAction("切换视频特效", this, &MainWindow::toggleEffect);
    menu_view->addAction("切换频谱可视化", this, &MainWindow::toggleVisualizer);

    // 工具菜单
    QMenu* menu_tools = menuBar()->addMenu("工具");
    menu_tools->addAction("连接数据库", this, &MainWindow::connectDatabaseDialog);
    menu_tools->addAction("格式转码", this, &MainWindow::transcodeDialog);

    // 帮助菜单
    QMenu* menu_help = menuBar()->addMenu("帮助");
    menu_help->addAction("快捷键说明", this, [this]() {
        QMessageBox::information(this, "快捷键说明",
            "SPACE - 播放/暂停\n"
            "LEFT/RIGHT - 后退/前进5秒\n"
            "UP/DOWN - 音量增大/减小\n"
            "PAGEUP/PAGEDOWN - 上一曲/下一曲\n"
            "ESC - 退出程序\n"
            "E - 切换视频特效\n"
            "V - 切换频谱可视化\n"
            "1/2/3/4 - 播放速度 0.5x/1.0x/1.5x/2.0x\n"
            "M - 静音/取消静音\n"
            "F - 切换收藏");
    });
    menu_help->addAction("关于", this, [this]() {
        QMessageBox::about(this, "关于 MediaStudio",
            "MediaStudio v1.0\n\n"
            "基于 FFmpeg + OpenGL + Qt 的音视频工作站\n\n"
            "技术栈:\n"
            "• FFmpeg 8.0 - 音视频编解码\n"
            "• OpenGL 3.3 - GPU渲染与特效\n"
            "• Qt 6.11 - 跨平台GUI框架\n"
            "• WASAPI - Windows音频播放\n"
            "• MySQL - 媒体库数据库");
    });
}

void MainWindow::setupToolBar()
{
    QToolBar* toolbar = addToolBar("主工具栏");
    toolbar->setMovable(false);
    toolbar->setIconSize(QSize(24, 24));

    // 打开文件按钮
    QPushButton* btn_open = new QPushButton("📂 打开");
    btn_open->setToolTip("打开媒体文件");
    btn_open->setMinimumHeight(32);
    toolbar->addWidget(btn_open);
    QObject::connect(btn_open, &QPushButton::clicked, this, &MainWindow::openFile);

    toolbar->addSeparator();

    // 上一曲
    btn_prev_ = new QPushButton("◀◀");
    btn_prev_->setToolTip("上一曲");
    btn_prev_->setMinimumHeight(32);
    btn_prev_->setMaximumWidth(40);
    toolbar->addWidget(btn_prev_);

    // 播放/暂停
    btn_play_ = new QPushButton("▶");
    btn_play_->setToolTip("播放/暂停 (Space)");
    btn_play_->setMinimumHeight(32);
    btn_play_->setMaximumWidth(50);
    btn_play_->setStyleSheet("QPushButton { font-size: 16px; font-weight: bold; }");
    toolbar->addWidget(btn_play_);

    // 停止
    btn_stop_ = new QPushButton("■");
    btn_stop_->setToolTip("停止");
    btn_stop_->setMinimumHeight(32);
    btn_stop_->setMaximumWidth(40);
    toolbar->addWidget(btn_stop_);

    // 下一曲
    btn_next_ = new QPushButton("▶▶");
    btn_next_->setToolTip("下一曲");
    btn_next_->setMinimumHeight(32);
    btn_next_->setMaximumWidth(40);
    toolbar->addWidget(btn_next_);

    toolbar->addSeparator();

    // 音量控制
    btn_mute_ = new QPushButton("🔊");
    btn_mute_->setToolTip("静音/取消静音 (M)");
    btn_mute_->setCheckable(true);
    btn_mute_->setMinimumHeight(32);
    btn_mute_->setMaximumWidth(40);
    btn_mute_->setObjectName("btn_mute");
    toolbar->addWidget(btn_mute_);

    slider_volume_ = new QSlider(Qt::Horizontal);
    slider_volume_->setRange(0, 100);
    slider_volume_->setValue(100);
    slider_volume_->setMaximumWidth(120);
    slider_volume_->setToolTip("音量控制");
    toolbar->addWidget(slider_volume_);

    toolbar->addSeparator();

    // 播放速度
    toolbar->addWidget(new QLabel(" 速度:"));
    combo_speed_ = new QComboBox();
    combo_speed_->addItems({"0.25x", "0.5x", "0.75x", "1.0x", "1.25x", "1.5x", "2.0x", "4.0x"});
    combo_speed_->setCurrentIndex(3);  // 默认1.0x
    combo_speed_->setMaximumWidth(80);
    toolbar->addWidget(combo_speed_);

    toolbar->addSeparator();

    // 特效按钮
    QPushButton* btn_effect = new QPushButton("🎨 特效");
    btn_effect->setToolTip("切换视频特效");
    btn_effect->setMinimumHeight(32);
    toolbar->addWidget(btn_effect);
    QObject::connect(btn_effect, &QPushButton::clicked, this, &MainWindow::toggleEffect);

    // 可视化按钮
    QPushButton* btn_viz = new QPushButton("🎵 频谱");
    btn_viz->setToolTip("切换音频频谱可视化");
    btn_viz->setMinimumHeight(32);
    toolbar->addWidget(btn_viz);
    QObject::connect(btn_viz, &QPushButton::clicked, this, &MainWindow::toggleVisualizer);
}

void MainWindow::setupCentralWidget()
{
    // 创建视频显示控件
    video_widget_ = new VideoWidget(this);
}

void MainWindow::setupDockWidgets()
{
    // ---- 播放列表停靠窗口 ----
    // dock_playlist_ 已在构造函数中创建
    dock_playlist_->setAllowedAreas(Qt::RightDockWidgetArea | Qt::BottomDockWidgetArea);

    QWidget* playlist_container = new QWidget();
    QVBoxLayout* playlist_layout = new QVBoxLayout(playlist_container);

    list_playlist_ = new QListWidget();
    list_playlist_->setAlternatingRowColors(true);
    playlist_layout->addWidget(list_playlist_);

    // 播放列表控制按钮
    QHBoxLayout* playlist_btn_layout = new QHBoxLayout();
    QPushButton* btn_add = new QPushButton("➕ 添加");
    QPushButton* btn_remove = new QPushButton("➖ 移除");
    QPushButton* btn_clear = new QPushButton("🗑 清空");
    playlist_btn_layout->addWidget(btn_add);
    playlist_btn_layout->addWidget(btn_remove);
    playlist_btn_layout->addWidget(btn_clear);
    playlist_layout->addLayout(playlist_btn_layout);

    QObject::connect(btn_add, &QPushButton::clicked, this, &MainWindow::addToPlaylist);
    QObject::connect(btn_remove, &QPushButton::clicked, this, &MainWindow::removeFromPlaylist);
    QObject::connect(btn_clear, &QPushButton::clicked, this, [this]() {
        list_playlist_->clear();
        playlist_.clear();
        current_playlist_index_ = 0;
    });
    QObject::connect(list_playlist_, &QListWidget::itemDoubleClicked,
        this, &MainWindow::playlistItemDoubleClicked);

    dock_playlist_->setWidget(playlist_container);
    addDockWidget(Qt::RightDockWidgetArea, dock_playlist_);

    // ---- 媒体信息停靠窗口 ----
    // dock_info_ 已在构造函数中创建
    dock_info_->setAllowedAreas(Qt::RightDockWidgetArea | Qt::BottomDockWidgetArea);

    QWidget* info_container = new QWidget();
    QVBoxLayout* info_layout = new QVBoxLayout(info_container);

    text_media_info_ = new QTextEdit();
    text_media_info_->setReadOnly(true);
    text_media_info_->setMinimumWidth(250);
    info_layout->addWidget(text_media_info_);

    dock_info_->setWidget(info_container);
    addDockWidget(Qt::RightDockWidgetArea, dock_info_);

    // 默认显示播放列表，隐藏信息面板
    dock_playlist_->show();
    dock_info_->hide();
}

void MainWindow::setupBottomBar()
{
    // 底部播放控制栏
    QWidget* bottom_bar = new QWidget();
    QVBoxLayout* bottom_layout = new QVBoxLayout(bottom_bar);
    bottom_layout->setContentsMargins(8, 4, 8, 4);

    // 进度条区域
    QHBoxLayout* progress_layout = new QHBoxLayout();

    label_time_ = new QLabel("00:00");
    label_time_->setMinimumWidth(50);
    label_time_->setAlignment(Qt::AlignCenter);

    slider_progress_ = new QSlider(Qt::Horizontal);
    slider_progress_->setRange(0, 1000);
    slider_progress_->setValue(0);
    slider_progress_->setToolTip("点击或拖动跳转播放位置");

    label_duration_ = new QLabel("00:00");
    label_duration_->setMinimumWidth(50);
    label_duration_->setAlignment(Qt::AlignCenter);

    progress_layout->addWidget(label_time_);
    progress_layout->addWidget(slider_progress_, 1);
    progress_layout->addWidget(label_duration_);

    bottom_layout->addLayout(progress_layout);

    // 状态栏
    statusBar()->showMessage("就绪 - 拖放或打开文件开始播放");

    // 将视频和底部栏组合为中央部件
    QWidget* central_container = new QWidget();
    QVBoxLayout* main_layout = new QVBoxLayout(central_container);
    main_layout->setContentsMargins(0, 0, 0, 0);
    main_layout->setSpacing(0);

    // 视频控件
    main_layout->addWidget(video_widget_, 1);
    main_layout->addWidget(bottom_bar);

    setCentralWidget(central_container);
}

// ============================================================
// 暗色主题
// ============================================================
void MainWindow::applyDarkTheme()
{
    QString style = R"(
        QMainWindow {
            background-color: #1e1e2e;
        }
        QWidget {
            background-color: #1e1e2e;
            color: #cdd6f4;
            font-family: "Microsoft YaHei", "Segoe UI", sans-serif;
        }
        QPushButton {
            background-color: #313244;
            color: #cdd6f4;
            border: 1px solid #45475a;
            border-radius: 6px;
            padding: 6px 14px;
            font-size: 13px;
        }
        QPushButton:hover {
            background-color: #45475a;
            border-color: #89b4fa;
        }
        QPushButton:pressed {
            background-color: #585b70;
        }
        QPushButton#btn_mute:checked {
            background-color: #f38ba8;
            color: #11111b;
        }
        QSlider::groove:horizontal {
            background: #313244;
            height: 6px;
            border-radius: 3px;
        }
        QSlider::handle:horizontal {
            background: #89b4fa;
            width: 16px;
            height: 16px;
            margin: -5px 0;
            border-radius: 8px;
        }
        QSlider::handle:horizontal:hover {
            background: #b4befe;
        }
        QSlider::sub-page:horizontal {
            background: #89b4fa;
            border-radius: 3px;
        }
        QListWidget {
            background-color: #181825;
            border: 1px solid #313244;
            border-radius: 4px;
            font-size: 13px;
        }
        QListWidget::item {
            padding: 6px;
            border-bottom: 1px solid #313244;
        }
        QListWidget::item:selected {
            background-color: #45475a;
            color: #89b4fa;
        }
        QListWidget::item:alternate {
            background-color: #1e1e2e;
        }
        QTextEdit {
            background-color: #181825;
            border: 1px solid #313244;
            border-radius: 4px;
            font-size: 12px;
            padding: 8px;
        }
        QComboBox {
            background-color: #313244;
            border: 1px solid #45475a;
            border-radius: 4px;
            padding: 4px 8px;
            color: #cdd6f4;
        }
        QComboBox::drop-down {
            border: none;
        }
        QComboBox QAbstractItemView {
            background-color: #313244;
            color: #cdd6f4;
            selection-background-color: #45475a;
        }
        QDockWidget {
            color: #cdd6f4;
            titlebar-close-icon: none;
        }
        QDockWidget::title {
            background-color: #313244;
            padding: 6px;
            border-bottom: 1px solid #45475a;
        }
        QMenuBar {
            background-color: #181825;
            color: #cdd6f4;
            border-bottom: 1px solid #313244;
        }
        QMenuBar::item:selected {
            background-color: #45475a;
        }
        QMenu {
            background-color: #313244;
            color: #cdd6f4;
            border: 1px solid #45475a;
        }
        QMenu::item:selected {
            background-color: #585b70;
        }
        QToolBar {
            background-color: #181825;
            border-bottom: 1px solid #313244;
            spacing: 4px;
            padding: 4px;
        }
        QStatusBar {
            background-color: #181825;
            color: #a6adc8;
            border-top: 1px solid #313244;
        }
        QLabel {
            background: transparent;
        }
    )";
    setStyleSheet(style);
}

// ============================================================
// 信号连接
// ============================================================
void MainWindow::connectSignals()
{
    // 将PlayerController与VideoWidget关联
    player_->setVideoWidget(video_widget_);

    // 播放控制按钮
    QObject::connect(btn_play_, &QPushButton::clicked, this, &MainWindow::togglePlayPause);
    QObject::connect(btn_stop_, &QPushButton::clicked, this, &MainWindow::stopPlayback);
    QObject::connect(btn_prev_, &QPushButton::clicked, this, &MainWindow::prevFile);
    QObject::connect(btn_next_, &QPushButton::clicked, this, &MainWindow::nextFile);
    QObject::connect(btn_mute_, &QPushButton::clicked, this, &MainWindow::toggleMute);

    // 进度条
    QObject::connect(slider_progress_, &QSlider::sliderMoved, this, &MainWindow::seekPosition);

    // 音量
    QObject::connect(slider_volume_, &QSlider::valueChanged, this, &MainWindow::changeVolume);

    // 速度
    QObject::connect(combo_speed_, QOverload<int>::of(&QComboBox::currentIndexChanged),
        this, &MainWindow::changeSpeed);

    // 键盘快捷键
    setupShortcuts();
}

void MainWindow::setupShortcuts()
{
    // 播放/暂停 (Space)
    QShortcut* sc_space = new QShortcut(QKeySequence(Qt::Key_Space), this);
    sc_space->setContext(Qt::ApplicationShortcut);
    connect(sc_space, &QShortcut::activated, this, &MainWindow::togglePlayPause);

    // 快进 (Right)
    QShortcut* sc_right = new QShortcut(QKeySequence(Qt::Key_Right), this);
    sc_right->setContext(Qt::ApplicationShortcut);
    connect(sc_right, &QShortcut::activated, this, [this]() {
        player_->seek(player_->getCurrentTime() + 5.0);
    });

    // 快退 (Left)
    QShortcut* sc_left = new QShortcut(QKeySequence(Qt::Key_Left), this);
    sc_left->setContext(Qt::ApplicationShortcut);
    connect(sc_left, &QShortcut::activated, this, [this]() {
        player_->seek(player_->getCurrentTime() - 5.0);
    });

    // 音量加 (Up)
    QShortcut* sc_up = new QShortcut(QKeySequence(Qt::Key_Up), this);
    sc_up->setContext(Qt::ApplicationShortcut);
    connect(sc_up, &QShortcut::activated, this, [this]() {
        slider_volume_->setValue(slider_volume_->value() + 5);
    });

    // 音量减 (Down)
    QShortcut* sc_down = new QShortcut(QKeySequence(Qt::Key_Down), this);
    sc_down->setContext(Qt::ApplicationShortcut);
    connect(sc_down, &QShortcut::activated, this, [this]() {
        slider_volume_->setValue(slider_volume_->value() - 5);
    });

    // 下一个 (PageDown)
    QShortcut* sc_next = new QShortcut(QKeySequence(Qt::Key_PageDown), this);
    sc_next->setContext(Qt::ApplicationShortcut);
    connect(sc_next, &QShortcut::activated, this, &MainWindow::nextFile);

    // 上一个 (PageUp)
    QShortcut* sc_prev = new QShortcut(QKeySequence(Qt::Key_PageUp), this);
    sc_prev->setContext(Qt::ApplicationShortcut);
    connect(sc_prev, &QShortcut::activated, this, &MainWindow::prevFile);

    // 倍速 (1-4)
    QShortcut* sc_speed1 = new QShortcut(QKeySequence(Qt::Key_1), this);
    sc_speed1->setContext(Qt::ApplicationShortcut);
    connect(sc_speed1, &QShortcut::activated, this, [this]() { combo_speed_->setCurrentIndex(1); });

    QShortcut* sc_speed2 = new QShortcut(QKeySequence(Qt::Key_2), this);
    sc_speed2->setContext(Qt::ApplicationShortcut);
    connect(sc_speed2, &QShortcut::activated, this, [this]() { combo_speed_->setCurrentIndex(3); });

    QShortcut* sc_speed3 = new QShortcut(QKeySequence(Qt::Key_3), this);
    sc_speed3->setContext(Qt::ApplicationShortcut);
    connect(sc_speed3, &QShortcut::activated, this, [this]() { combo_speed_->setCurrentIndex(5); });

    QShortcut* sc_speed4 = new QShortcut(QKeySequence(Qt::Key_4), this);
    sc_speed4->setContext(Qt::ApplicationShortcut);
    connect(sc_speed4, &QShortcut::activated, this, [this]() { combo_speed_->setCurrentIndex(6); });

    // 收藏 (F)
    QShortcut* sc_fav = new QShortcut(QKeySequence(Qt::Key_F), this);
    sc_fav->setContext(Qt::ApplicationShortcut);
    connect(sc_fav, &QShortcut::activated, this, [this]() {
        if (db_connected_) {
            MediaInfo info = player_->getMediaInfo();
            if (info.db_id > 0) {
                bool new_state = !info.is_favorite;
                if (database_->setFavorite(info.db_id, new_state)) {
                    info.is_favorite = new_state;
                    player_->setMediaInfo(info);
                    statusBar()->showMessage(new_state ? "已添加收藏" : "已取消收藏");
                }
            }
        }
    });

    // 静音 (M)
    QShortcut* sc_mute = new QShortcut(QKeySequence(Qt::Key_M), this);
    sc_mute->setContext(Qt::ApplicationShortcut);
    connect(sc_mute, &QShortcut::activated, this, &MainWindow::toggleMute);
}

// ============================================================
// 槽函数实现
// ============================================================
void MainWindow::openFile()
{
    QString filename = QFileDialog::getOpenFileName(this,
        "打开媒体文件", "",
        "媒体文件 (*.mp4 *.mkv *.avi *.mov *.flv *.wmv *.mp3 *.wav);;所有文件 (*.*)");

    if (filename.isEmpty()) return;

    std::string file_path = filename.toStdString();

    // 添加到播放列表
    playlist_.push_back(file_path);
    current_playlist_index_ = (int)playlist_.size() - 1;
    list_playlist_->addItem(QFileInfo(filename).fileName());

    // 加载并播放
    // decoder->open + avformat_find_stream_info 可能阻塞数秒，必须在后台线程执行
    requestLoadAndPlay(file_path, QFileInfo(filename).fileName());
}

void MainWindow::togglePlayPause()//暂停
{
    if (!player_->isPlaying() && !player_->isPaused()) {
        if (playlist_.empty()) {
            openFile();
            return;
        }
        // 需要加载文件，异步执行
        std::string path = playlist_[current_playlist_index_];
        requestLoadAndPlay(path, QFileInfo(QString::fromStdString(path)).fileName());
        return;
    }

    player_->togglePlayPause();

    if (player_->isPaused()) {
        btn_play_->setText("▶");
        statusBar()->showMessage("已暂停");
    }
    else {
        btn_play_->setText("⏸");
        statusBar()->showMessage("正在播放");
    }
}

void MainWindow::stopPlayback()//停止
{
    player_->stopAsync();
    btn_play_->setText("▶");
    video_widget_->clearFrame();
    slider_progress_->setValue(0);
    label_time_->setText("00:00");
    statusBar()->showMessage("已停止");
}

void MainWindow::seekPosition(int position)
{
    double duration = player_->getDuration();
    if (duration <= 0) return;

    double seek_time = (position / 1000.0) * duration;
    player_->seek(seek_time);
}

void MainWindow::changeVolume(int volume)
{
    player_->setVolume(volume / 100.0f);
    is_muted_ = (volume == 0);
    if (!is_muted_) {
        last_volume_ = volume;
    }
    
    // 同步更新静音按钮状态
    btn_mute_->blockSignals(true);
    btn_mute_->setChecked(is_muted_);
    btn_mute_->setText(is_muted_ ? "🔇" : "🔊");
    btn_mute_->blockSignals(false);

    statusBar()->showMessage(QString("音量: %1%").arg(volume));
}

void MainWindow::changeSpeed(int index)
{
    QStringList speeds = {"0.25", "0.5", "0.75", "1.0", "1.25", "1.5", "2.0", "4.0"};
    if (index >= 0 && index < speeds.size()) {
        float speed = speeds[index].toFloat();
        player_->setSpeed(speed);
        statusBar()->showMessage(QString("播放速度: %1x").arg(speeds[index]));
    }
}

void MainWindow::nextFile()
{
    if (playlist_.empty()) return;
    current_playlist_index_ = (current_playlist_index_ + 1) % (int)playlist_.size();
    list_playlist_->setCurrentRow(current_playlist_index_);
    std::string path = playlist_[current_playlist_index_];
    requestLoadAndPlay(path, QFileInfo(QString::fromStdString(path)).fileName());
}

void MainWindow::prevFile()
{
    if (playlist_.empty()) return;
    current_playlist_index_ = (current_playlist_index_ - 1 + (int)playlist_.size()) % (int)playlist_.size();
    list_playlist_->setCurrentRow(current_playlist_index_);
    std::string path = playlist_[current_playlist_index_];
    requestLoadAndPlay(path, QFileInfo(QString::fromStdString(path)).fileName());
}

void MainWindow::toggleEffect()
{
    VideoEffect current = player_->getEffect();
    int next = (static_cast<int>(current) + 1) % static_cast<int>(VideoEffect::Count);
    VideoEffect new_effect = static_cast<VideoEffect>(next);
    player_->setEffect(new_effect);
    video_widget_->setEffect(new_effect);
    statusBar()->showMessage(QString("视频特效: %1").arg(
        QString::fromStdString(videoEffectToString(new_effect))));
}

void MainWindow::toggleVisualizer()
{
    player_->toggleVisualizer();
    bool showing = player_->isVisualizerShown();
    video_widget_->setShowSpectrum(showing);
    statusBar()->showMessage(showing ? "频谱可视化模式" : "视频播放模式");
}

void MainWindow::connectDatabaseDialog()
{
    bool ok;
    QString host = QInputDialog::getText(this, "数据库连接", "主机地址:",
        QLineEdit::Normal, "127.0.0.1", &ok);
    if (!ok) return;

    QString user = QInputDialog::getText(this, "数据库连接", "用户名:",
        QLineEdit::Normal, "root", &ok);
    if (!ok) return;

    QString password = QInputDialog::getText(this, "数据库连接", "密码:",
        QLineEdit::Password, "", &ok);
    if (!ok) return;

    QString database = QInputDialog::getText(this, "数据库连接", "数据库名:",
        QLineEdit::Normal, "media_center", &ok);
    if (!ok) return;

    if (connectDatabase(host.toStdString(), user.toStdString(),
                        password.toStdString(), database.toStdString())) {
        QMessageBox::information(this, "成功", "数据库连接成功！");
        db_connected_ = true;
    }
    else {
        QMessageBox::warning(this, "失败",
            "数据库连接失败:\n" + QString::fromStdString(database_->getLastError()));
    }
}

void MainWindow::transcodeDialog()
{
    if (transcoding_.load()) {
        QMessageBox::information(this, "提示", "已有转码任务正在执行，请稍候。");
        return;
    }

    QString input = QFileDialog::getOpenFileName(this, "选择源文件", "",
        "媒体文件 (*.mp4 *.mkv *.avi *.mov *.flv);;所有文件 (*.*)");
    if (input.isEmpty()) return;

    QString output = QFileDialog::getSaveFileName(this, "保存转码文件", "",
        "MP4 (*.mp4);;MKV (*.mkv);;AVI (*.avi)");
    if (output.isEmpty()) return;

    transcoding_ = true;
    statusBar()->showMessage("正在转码...");
    if (transcoding_thread_.joinable()) {
        transcoding_thread_.join();
    }
    transcoding_thread_ = std::thread([this, input, output]() {
        bool success = transcoder_->transcode(
            input.toStdString(), output.toStdString(),
            0, 0, "libx264", "aac", 0,
            [this](float progress) {
                QMetaObject::invokeMethod(this, [this, progress]() {
                    statusBar()->showMessage(QString("转码进度: %1%").arg((int)(progress * 100)));
                }, Qt::QueuedConnection);
            }
        );

        QMetaObject::invokeMethod(this, [this, success, output]() {
            transcoding_ = false;
            if (success) {
                statusBar()->showMessage("转码完成: " + output);
                QMessageBox::information(this, "完成", "转码完成！");
            }
            else {
                statusBar()->showMessage("转码失败");
                QMessageBox::warning(this, "失败",
                    "转码失败:\n" + QString::fromStdString(transcoder_->getLastError()));
            }
        }, Qt::QueuedConnection);
    });
}

void MainWindow::addToPlaylist()
{
    QStringList files = QFileDialog::getOpenFileNames(this, "添加文件到播放列表", "",
        "媒体文件 (*.mp4 *.mkv *.avi *.mov *.flv *.wmv *.mp3 *.wav);;所有文件 (*.*)");

    for (const QString& file : files) {
        playlist_.push_back(file.toStdString());
        list_playlist_->addItem(QFileInfo(file).fileName());
    }
}

void MainWindow::removeFromPlaylist()
{
    int row = list_playlist_->currentRow();
    if (row < 0) return;

    list_playlist_->takeItem(row);
    playlist_.erase(playlist_.begin() + row);
    if (current_playlist_index_ >= (int)playlist_.size()) {
        current_playlist_index_ = 0;
    }
}

void MainWindow::playlistItemDoubleClicked(QListWidgetItem* item)
{
    int row = list_playlist_->row(item);
    if (row < 0 || row >= (int)playlist_.size()) return;

    current_playlist_index_ = row;
    std::string path = playlist_[row];
    requestLoadAndPlay(path, item->text());
}

void MainWindow::updatePlaybackInfo()
{
    if (!player_->isPlaying() && !player_->isPaused()) return;

    double current = player_->getCurrentTime();
    double duration = player_->getDuration();

    if (duration > 0) {
        // 避免在用户拖动进度条时更新
        if (!slider_progress_->isSliderDown()) {
            slider_progress_->blockSignals(true);
            slider_progress_->setValue((int)(current / duration * 1000));
            slider_progress_->blockSignals(false);
        }

        // 时间格式化
        auto formatTime = [](double seconds) -> QString {
            int mins = (int)(seconds / 60);
            int secs = (int)(seconds) % 60;
            return QString("%1:%2").arg(mins, 2, 10, QChar('0')).arg(secs, 2, 10, QChar('0'));
        };

        label_time_->setText(formatTime(current));
        label_duration_->setText(formatTime(duration));
    }

    // 更新媒体信息面板
    const MediaInfo& info = player_->getMediaInfo();
    if (!info.file_name.empty()) {
        QString info_text;
        info_text += QString("<h3>%1</h3>").arg(QString::fromStdString(info.file_name));
        info_text += QString("<p>格式: %1</p>").arg(QString::fromStdString(info.format_name));
        info_text += QString("<p>时长: %1秒</p>").arg(info.duration, 0, 'f', 1);

        if (info.has_video) {
            info_text += "<hr><h4>视频</h4>";
            info_text += QString("<p>编码: %1</p>").arg(QString::fromStdString(info.video_info.codec_name));
            info_text += QString("<p>分辨率: %1x%2</p>").arg(info.video_info.width).arg(info.video_info.height);
            info_text += QString("<p>帧率: %1 fps</p>").arg(info.video_info.frame_rate, 0, 'f', 1);
            info_text += QString("<p>特效: %1</p>").arg(
                QString::fromStdString(videoEffectToString(player_->getEffect())));
        }

        if (info.has_audio) {
            info_text += "<hr><h4>音频</h4>";
            info_text += QString("<p>编码: %1</p>").arg(QString::fromStdString(info.audio_info.codec_name));
            info_text += QString("<p>采样率: %1 Hz</p>").arg(info.audio_info.sample_rate);
            info_text += QString("<p>声道: %1</p>").arg(info.audio_info.channels);
        }

        text_media_info_->setHtml(info_text);
    }
}

void MainWindow::toggleMute()
{
    if (is_muted_) {
        // 解除静音，恢复之前的音量
        slider_volume_->setValue(last_volume_);
        is_muted_ = false;
        btn_mute_->setChecked(false);
        btn_mute_->setText("🔊");
        statusBar()->showMessage("解除静音", 2000);
    } else {
        // 静音，保存当前音量
        last_volume_ = slider_volume_->value();
        slider_volume_->setValue(0);
        is_muted_ = true;
        btn_mute_->setChecked(true);
        btn_mute_->setText("🔇");
        statusBar()->showMessage("静音", 2000);
    }
}

// ============================================================
// 辅助方法
// ============================================================
void MainWindow::requestLoadAndPlay(const std::string& path, const QString& display_name)
{
    bool need_pause = player_->isPlaying() && !player_->isPaused();
    if (need_pause) {
        player_->pause();
        btn_play_->setText("▶");
    }

    video_widget_->clearFrame();

    int request_id = ++load_request_id_;
    statusBar()->showMessage(QString("%1正在加载: %2...")
        .arg(need_pause ? "已自动暂停，" : "")
        .arg(display_name));

    std::thread([this, path, display_name, request_id]() {
        if (request_id != load_request_id_.load()) {
            return;
        }

        bool loaded = false;
        {
            std::lock_guard<std::mutex> lock(load_mutex_);
            if (request_id != load_request_id_.load()) {
                return;
            }

            loaded = player_->loadFile(path);
            if (loaded && request_id == load_request_id_.load()) {
                // 数据库同步逻辑
                if (db_connected_) {
                    MediaInfo info = player_->getMediaInfo();
                    MediaInfo db_info;
                    if (database_->getMediaByPath(path, db_info)) {
                        // 已存在，同步数据库中的 ID 和收藏状态
                        info.db_id = db_info.db_id;
                        info.is_favorite = db_info.is_favorite;
                        info.title = db_info.title;
                        info.tags = db_info.tags;
                    }
                    else {
                        // 不存在，添加到数据库
                        int id = database_->addMedia(info);
                        if (id > 0) {
                            info.db_id = id;
                        }
                    }

                    // 更新 PlayerController 中的信息（包含 db_id）
                    player_->setMediaInfo(info);

                    // 记录播放历史
                    if (info.db_id > 0) {
                        database_->recordPlayHistory(info.db_id);
                    }
                }

                player_->play();
                player_->pause();
                player_->play();
            }
        }

        if (!loaded || request_id != load_request_id_.load()) {
            return;
        }

        QMetaObject::invokeMethod(this, [this, display_name, request_id]() {
            if (request_id != load_request_id_.load()) {
                return;
            }
            btn_play_->setText("⏸");
            statusBar()->showMessage("正在播放: " + display_name);
        }, Qt::QueuedConnection);
    }).detach();
}

void MainWindow::setInitialFiles(const std::vector<std::string>& files)
{
    if (files.empty()) return;

    for (const auto& f : files) {
        playlist_.push_back(f);
        QString name = QString::fromStdString(f);
        int pos = name.lastIndexOf("/\\");
        if (pos >= 0) name = name.mid(pos + 1);
        list_playlist_->addItem(name);
    }

    current_playlist_index_ = 0;
    requestLoadAndPlay(files[0], QFileInfo(QString::fromStdString(files[0])).fileName());
}

bool MainWindow::connectDatabase(const std::string& host, const std::string& user,
                                  const std::string& password, const std::string& database)
{
    if (database_->connect(host, user, password, database)) {
        return database_->initTables();
    }
    return false;
}

// ============================================================
// 键盘事件
// ============================================================
void MainWindow::keyPressEvent(QKeyEvent* event)
{
    // 强制捕获 Esc 键退出程序，无论焦点在哪
    if (event->key() == Qt::Key_Escape) {
        close();
        return;
    }

    QMainWindow::keyPressEvent(event);
}

// 拖放支持
void MainWindow::dragEnterEvent(QDragEnterEvent* event)
{
    if (event->mimeData()->hasUrls()) {
        event->acceptProposedAction();
    }
}

void MainWindow::dropEvent(QDropEvent* event)
{
    const QList<QUrl> urlList = event->mimeData()->urls();
    for (const QUrl& url : urlList) {
        QString file = url.toLocalFile();
        if (!file.isEmpty()) {
            playlist_.push_back(file.toStdString());
            list_playlist_->addItem(QFileInfo(file).fileName());
        }
    }

    if (!playlist_.empty() && !player_->isPlaying()) {
        current_playlist_index_ = (int)playlist_.size() - 1;
        requestLoadAndPlay(playlist_[current_playlist_index_], QFileInfo(QString::fromStdString(playlist_[current_playlist_index_])).fileName());
    }
}
