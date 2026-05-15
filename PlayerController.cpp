/**
 * @file PlayerController.cpp
 * @brief 播放控制器实现 - Qt版本
 * 
 * Qt版本改动：
 * - 视频帧通过VideoWidget::setVideoFrame()传递（线程安全）
 * - 频谱数据通过VideoWidget::setSpectrumData()传递
 * - 不再直接调用OpenGL渲染（由VideoWidget在Qt线程中处理）
 * - 解码线程只负责解码和数据传递
 */

#include "PlayerController.h"
#include "VideoWidget.h"
#include "AudioVisualizer.h"
#include <iostream>
#include <chrono>
#include <algorithm>
#include <cmath>

PlayerController::PlayerController()
    : video_widget_(nullptr)
    , playing_(false)
    , paused_(false)
    , stop_requested_(false)
    , session_id_(0)
    , current_time_(0.0)
    , duration_(0.0)
    , volume_(1.0f)
    , speed_(1.0f)
    , seek_requested_(false)
    , pending_seek_seconds_(0.0)
    , current_effect_(VideoEffect::None)
    , show_visualizer_(false)
{
    decoder_ = std::make_unique<MediaDecoder>();
    audio_output_ = std::make_unique<AudioOutput>();
}

PlayerController::~PlayerController()
{
    stop();
}

// ============================================================
// 加载文件
// ============================================================
bool PlayerController::loadFile(const std::string& filename)
{
    std::lock_guard<std::recursive_mutex> lifecycle_lock(lifecycle_mutex_);

    // 必须等待旧线程完全退出再操作decoder_，避免数据竞争
    // loadFile总是在后台线程中调用，所以这里的join不会阻塞主线程
    stop();

    if (!decoder_->open(filename)) {
        std::cerr << "[Player] 打开文件失败: " << filename << std::endl;
        return false;
    }

    if (!decoder_->setPlaybackSpeed(speed_.load())) {
        std::cerr << "[Player] 初始化播放速度失败" << std::endl;
    }

    // 更新状态
    media_info_ = decoder_->getMediaInfo();
    duration_ = media_info_.duration;
    current_time_ = 0.0;

    std::cout << "[Player] 文件加载成功: " << filename << std::endl;
    return true;
}

// ============================================================
// 播放控制
// ============================================================
void PlayerController::play()
{
    std::lock_guard<std::recursive_mutex> lifecycle_lock(lifecycle_mutex_);

    if (playing_ && !paused_) return;

    if (paused_) {
        paused_ = false;
        return;
    }

    playing_ = true;
    paused_ = false;
    stop_requested_ = false;
    ++session_id_;   // 新session，旧线程会因sid不匹配而退出

    // 启动单播放线程
    playback_thread_ = std::thread(&PlayerController::playbackThread, this);
}

void PlayerController::pause()
{
    paused_ = true;
}

void PlayerController::stopAsync()
{
    // 仅设置标志，立即返回，主线程不阻塞
    stop_requested_ = true;
    playing_ = false;
    paused_ = false;
    seek_requested_ = false;
    current_time_ = 0.0;
}

void PlayerController::stop()
{
    std::lock_guard<std::recursive_mutex> lifecycle_lock(lifecycle_mutex_);

    // 设置标志 + 等待线程结束
    // 只在后台线程或析构中调用，不能在主线程上调用
    stopAsync();
    if (playback_thread_.joinable()) playback_thread_.join();
}

void PlayerController::seek(double seconds)
{
    seconds = std::max(0.0, std::min(seconds, duration_.load()));
    pending_seek_seconds_ = seconds;
    seek_requested_ = true;
    current_time_ = seconds;
}

void PlayerController::setVolume(float volume)
{
    volume_ = std::max(0.0f, std::min(1.0f, volume));
}

void PlayerController::setSpeed(float speed)
{
    speed_ = std::max(0.25f, std::min(4.0f, speed));
}

void PlayerController::setEffect(VideoEffect effect)
{
    current_effect_ = effect;
}

void PlayerController::togglePlayPause()
{
    if (playing_ && !paused_) {
        pause();
    }
    else {
        play();
    }
}

// ============================================================
// 单线程播放循环
// 
// 单个线程顺序执行：demux -> decode -> audio/video output
// ============================================================
void PlayerController::playbackThread()
{
    AudioVisualizer fft_calculator;
    fft_calculator.init(64, false);

    bool audio_ready = false;
    bool audio_paused = false;
    if (decoder_->hasAudio()) {
        audio_ready = audio_output_->init(decoder_->getOutputAudioSampleRate(), decoder_->getOutputAudioChannels());
        if (!audio_ready) {
            std::cerr << "[Player] 音频输出初始化失败" << std::endl;
        }
        else {
            std::cout << "[Player] 音频输出已就绪: "
                      << decoder_->getOutputAudioSampleRate() << "Hz, "
                      << decoder_->getOutputAudioChannels() << "ch" << std::endl;
        }
    }

    auto playback_anchor = std::chrono::steady_clock::now();
    double anchor_pts = current_time_.load();
    const int my_session = session_id_.load();
    float active_speed = std::max(0.25f, speed_.load());
    bool audio_clock_active = false;
    double audio_clock_base = current_time_.load();

    auto get_audio_clock = [&]() -> double {
        if (!audio_ready || !audio_clock_active) return current_time_.load();
        return audio_clock_base + audio_output_->getPlayedSeconds() * active_speed;
    };

    while (!stop_requested_ && playing_ && session_id_.load() == my_session) {
        if (paused_) {
            if (audio_ready && !audio_paused) {
                audio_output_->pause();
                audio_paused = true;
            }
            std::this_thread::sleep_for(std::chrono::milliseconds(10));
            playback_anchor = std::chrono::steady_clock::now();
            anchor_pts = current_time_.load();
            continue;
        }

        float requested_speed = std::max(0.25f, speed_.load());
        if (std::fabs(requested_speed - active_speed) > 0.001f) {
            double sync_time = audio_clock_active ? get_audio_clock() : current_time_.load();
            if (!decoder_->setPlaybackSpeed(requested_speed)) {
                std::cerr << "[Player] 更新播放速度失败: " << requested_speed << "x" << std::endl;
            }
            active_speed = requested_speed;
            if (audio_ready) {
                audio_output_->reset();
                audio_paused = false;
                audio_clock_active = false;
            }
            current_time_ = sync_time;
            playback_anchor = std::chrono::steady_clock::now();
            anchor_pts = sync_time;
        }

        if (audio_ready && audio_paused) {
            audio_output_->resume();
            audio_paused = false;
        }

        if (seek_requested_) {
            std::lock_guard<std::mutex> lock(seek_mutex_);
            double seek_seconds = pending_seek_seconds_.load();
            decoder_->seek(seek_seconds);
            //暂停是为了清空音频缓冲区，等待新数据就位，下次循环就会执行resume来播放
            if (audio_ready) {
                audio_output_->reset();
                audio_output_->pause();
                audio_paused = true;
                audio_clock_active = false;
            }
            current_time_ = seek_seconds;
            seek_requested_ = false;
            decoder_->setPlaybackSpeed(active_speed);
            playback_anchor = std::chrono::steady_clock::now();
            anchor_pts = seek_seconds;
        }

        DecodedFrame frame;

        {
            std::lock_guard<std::mutex> lock(seek_mutex_);
            if (!decoder_->decodeNextFrame(frame)) {
                playing_ = false;
                break;
            }
        }

        if (frame.type == DecodedFrame::Type::Video) {
            bool display_frame = true;

            if (audio_ready && audio_clock_active) {
                constexpr double sync_threshold = 0.04;
                constexpr double drop_threshold = 0.12;
                constexpr double max_wait_seconds = 0.02;
                constexpr double min_audio_queue_for_wait = 0.12;
                constexpr double audio_queue_floor = 0.08;

                double diff = frame.pts - get_audio_clock();
                double queued_seconds = audio_output_->getQueuedSeconds();
                if (diff > sync_threshold && queued_seconds > min_audio_queue_for_wait) {
                    double wait_seconds = std::min(diff, max_wait_seconds);
                    wait_seconds = std::min(wait_seconds, queued_seconds - audio_queue_floor);
                    if (wait_seconds > 0.0) {
                        auto wait_until = std::chrono::steady_clock::now() +
                            std::chrono::duration_cast<std::chrono::steady_clock::duration>(
                                std::chrono::duration<double>(wait_seconds));
                        while (!stop_requested_ && !paused_ && std::chrono::steady_clock::now() < wait_until) {
                            std::this_thread::sleep_for(std::chrono::milliseconds(1));
                        }
                        diff = frame.pts - get_audio_clock();
                    }
                }

                if (diff < -drop_threshold) {
                    display_frame = false;
                }
            }
            else if (!audio_ready) {
                double target_delay = (frame.pts - anchor_pts) / active_speed;
                if (target_delay > 0.0) {
                    auto target_time = playback_anchor + std::chrono::duration_cast<std::chrono::steady_clock::duration>(
                        std::chrono::duration<double>(target_delay));
                    while (!stop_requested_ && !paused_ && std::chrono::steady_clock::now() < target_time) {
                        std::this_thread::sleep_for(std::chrono::milliseconds(1));
                    }
                }
            }

            if (display_frame && frame.data && video_widget_) {
                video_widget_->setVideoFrame(frame.data, frame.width, frame.height);
            }
            current_time_ = (audio_ready && audio_clock_active) ? get_audio_clock() : frame.pts;
        }
        else if (frame.type == DecodedFrame::Type::Audio) {
            if (frame.data && frame.data_size > 0) {
                std::vector<uint8_t> volume_data(frame.data, frame.data + frame.data_size);

                if (volume_ < 1.0f) {
                    int16_t* pcm = (int16_t*)volume_data.data();
                    int sample_count = frame.data_size / 2;
                    for (int i = 0; i < sample_count; i++) {
                        pcm[i] = (int16_t)(pcm[i] * volume_);
                    }
                }

                bool audio_written = false;
                if (audio_ready) {
                    if (!audio_output_->play(volume_data)) {
                        std::cerr << "[Player] 音频写入失败: " << frame.data_size << " bytes" << std::endl;
                    }
                    else {
                        audio_written = true;
                    }
                }

                if (video_widget_ && show_visualizer_) {
                    fft_calculator.processAudioData(volume_data.data(), frame.data_size,
                        decoder_->getOutputAudioChannels(), decoder_->getOutputAudioSampleRate());
                    video_widget_->setSpectrumData(fft_calculator.getSpectrumData());
                }

                if (audio_written) {
                    if (!audio_clock_active) {
                        audio_clock_base = frame.pts;
                        audio_clock_active = true;
                    }
                    current_time_ = get_audio_clock();
                }
                else if (!decoder_->hasVideo()) {
                    current_time_ = frame.pts;
                }
            }
        }

        if (frame.data) {
            av_freep(&frame.data);
        }
    }

    if (audio_ready) {
        audio_output_->cleanup();
    }
    playing_ = false;
}
