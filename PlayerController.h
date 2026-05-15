/**
 * @file PlayerController.h
 * @brief 播放控制器 - 管理播放状态、线程同步、播放列表
 * 
 * Qt版本：视频帧通过VideoWidget共享缓冲区传递，不直接调用OpenGL
 * 
 * 线程模型：
 * - Qt主线程：GUI + VideoWidget OpenGL渲染
 * - 视频解码线程：解码帧 → 传递给VideoWidget
 * - 音频解码线程：解码音频 → WASAPI播放 → 频谱数据传递
 */

#pragma once

#include <string>
#include <memory>
#include <atomic>
#include <mutex>
#include <thread>

#include "MediaInfo.h"
#include "MediaDecoder.h"
#include "AudioOutput.h"
#include "PacketQueue.h"

class VideoWidget;  // 前向声明，避免头文件依赖

class PlayerController {
public:
    PlayerController();
    ~PlayerController();

    /**
     * @brief 加载媒体文件
     * @param filename 文件路径
     * @return true=成功
     */
    bool loadFile(const std::string& filename);

    /** @brief 开始/继续播放 */
    void play();

    /** @brief 暂停播放 */
    void pause();

    /** @brief 停止播放 */
    void stop();

    /** @brief 跳转到指定时间 */
    void seek(double seconds);

    /** @brief 设置音量（0.0~1.0） */
    void setVolume(float volume);

    /** @brief 设置播放速度（0.25~4.0） */
    void setSpeed(float speed);

    /** @brief 切换视频特效 */
    void setEffect(VideoEffect effect);

    /** @brief 设置VideoWidget（用于帧数据传递） */
    void setVideoWidget(VideoWidget* widget) { video_widget_ = widget; }

    // 状态查询
    bool isPlaying() const { return playing_.load(); }
    bool isPaused() const { return paused_.load(); }
    double getCurrentTime() const { return current_time_.load(); }
    double getDuration() const { return duration_.load(); }
    float getVolume() const { return volume_.load(); }
    float getSpeed() const { return speed_.load(); }
    VideoEffect getEffect() const { return current_effect_; }
    const MediaInfo& getMediaInfo() const { return media_info_; }
    void setMediaInfo(const MediaInfo& info) { media_info_ = info; }

    /** @brief 切换播放/暂停 */
    void togglePlayPause();

    /** @brief 异步停止（只设置标志，不等待线程结束，供UI线程调用） */
    void stopAsync();

    /** @brief 切换可视化模式 */
    void toggleVisualizer() { show_visualizer_ = !show_visualizer_; }
    bool isVisualizerShown() const { return show_visualizer_; }

private:
    /** @brief 单线程播放循环（顺序解复用/解码/输出） */
    void playbackThread();

    // 核心组件
    std::unique_ptr<MediaDecoder> decoder_;
    std::unique_ptr<AudioOutput> audio_output_;
    PacketQueue audio_packets_;
    PacketQueue video_packets_;
    VideoWidget* video_widget_;

    // 原子状态变量（线程安全，无需加锁）
    std::atomic<bool> playing_;
    std::atomic<bool> paused_;
    std::atomic<bool> stop_requested_;
    std::atomic<int>  session_id_;   // 每次play()递增，旧线程检测到不匹配则退出
    std::atomic<double> current_time_;
    std::atomic<double> duration_;
    std::atomic<float> volume_;
    std::atomic<float> speed_;
    std::atomic<bool> seek_requested_;
    std::atomic<double> pending_seek_seconds_;

    // 线程控制
    std::thread playback_thread_;
    std::recursive_mutex lifecycle_mutex_;
    std::mutex seek_mutex_;  // 播放线程内部执行seek/解码时互斥

    // 当前状态
    MediaInfo media_info_;
    VideoEffect current_effect_;
    bool show_visualizer_;
};
