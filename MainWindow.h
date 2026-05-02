/**
 * @file MainWindow.h
 * @brief 主窗口 - Qt桌面应用界面
 * 
 * 界面布局：
 * ┌──────────────────────────────────────────────────┐
 * │ 菜单栏: 文件 | 视图 | 工具 | 帮助                │
 * ├──────────────────────────────────────────────────┤
 * │ 工具栏: [打开][◀◀][▶][▶▶][■] 音量[===] 速度     │
 * ├────────────────────────────┬─────────────────────┤
 * │                            │  📋 播放列表        │
 * │     视频显示区域            │  ─────────────      │
 * │     (VideoWidget)          │  > video1.mp4       │
 * │                            │    video2.mkv        │
 * │                            ├─────────────────────┤
 * │                            │  ℹ️ 媒体信息        │
 * │                            │  ─────────────      │
 * │                            │  编码: h264          │
 * │                            │  分辨率: 1920x1080   │
 * ├────────────────────────────┴─────────────────────┤
 * │ [00:00] ═══════●════════════════════ [03:45] 1x │
 * ├──────────────────────────────────────────────────┤
 * │ 状态栏: 正在播放 - video.mp4                     │
 * └──────────────────────────────────────────────────┘
 * 
 * Qt关键概念：
 * - QMainWindow：主窗口类，支持菜单栏/工具栏/状态栏/停靠窗口
 * - QDockWidget：可拖拽/停靠/隐藏的侧边面板
 * - Signals/Slots：Qt的事件通信机制（替代回调函数）
 * - QStyleSheet：Qt样式表（类似CSS，用于自定义外观）
 */

#pragma once

#include <QMainWindow>
#include <QPushButton>
#include <QSlider>
#include <QLabel>
#include <QListWidget>
#include <QTextEdit>
#include <QComboBox>
#include <QDockWidget>
#include <QTabWidget>
#include <QTimer>
#include <QAction>
#include <QMenuBar>

#include <atomic>
#include <mutex>

#include "VideoWidget.h"
#include "PlayerController.h"
#include "MediaDatabase.h"
#include "MediaTranscoder.h"

class MainWindow : public QMainWindow {
    Q_OBJECT

public:
    explicit MainWindow(QWidget* parent = nullptr);
    ~MainWindow();

    /**
     * @brief 设置初始播放文件
     * @param files 文件路径列表
     */
    void setInitialFiles(const std::vector<std::string>& files);

    /**
     * @brief 连接数据库
     */
    bool connectDatabase(const std::string& host, const std::string& user,
                         const std::string& password, const std::string& database);

private slots:
    // 播放控制槽函数（由UI事件触发）
    void openFile();
    void togglePlayPause();
    void stopPlayback();
    void seekPosition(int position);
    void changeVolume(int volume);
    void changeSpeed(int index);
    void nextFile();
    void prevFile();
    void toggleEffect();
    void toggleVisualizer();
    void connectDatabaseDialog();
    void transcodeDialog();
    void addToPlaylist();
    void removeFromPlaylist();
    void playlistItemDoubleClicked(QListWidgetItem* item);
    void updatePlaybackInfo();

private:
    // UI构建函数
    void setupUI();
    void setupMenuBar();
    void setupToolBar();
    void setupCentralWidget();
    void setupDockWidgets();
    void setupBottomBar();
    void setupShortcuts();
    void applyDarkTheme();
    void connectSignals();
    void requestLoadAndPlay(const std::string& path, const QString& display_name);

    // Qt事件重写
    void keyPressEvent(QKeyEvent* event) override;
    void dragEnterEvent(QDragEnterEvent* event) override;
    void dropEvent(QDropEvent* event) override;

    // 核心组件
    VideoWidget* video_widget_;
    PlayerController* player_;
    MediaDatabase* database_;
    MediaTranscoder* transcoder_;

    // UI控件
    QPushButton* btn_play_;
    QPushButton* btn_stop_;
    QPushButton* btn_prev_;
    QPushButton* btn_next_;
    QSlider* slider_progress_;
    QSlider* slider_volume_;
    QLabel* label_time_;
    QLabel* label_duration_;
    QComboBox* combo_speed_;
    QListWidget* list_playlist_;
    QTextEdit* text_media_info_;
    QDockWidget* dock_playlist_;
    QDockWidget* dock_info_;

    // 状态更新定时器
    QTimer info_timer_;

    // 播放列表数据
    std::vector<std::string> playlist_;
    int current_playlist_index_;
    std::mutex load_mutex_;
    std::atomic<int> load_request_id_{ 0 };

    // 数据库连接状态
    bool db_connected_;
};
