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
    audio_packets_.reset();
    video_packets_.reset();
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
    audio_packets_.abort();
    video_packets_.abort();
    audio_output_->reset();
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
    bool audio_ready = false;
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

    const int my_session = session_id_.load();
    const bool use_audio_queue = decoder_->hasAudio() && audio_ready;
    const bool use_video_queue = decoder_->hasVideo();
    std::atomic<float> active_speed(std::max(0.25f, speed_.load()));
    std::atomic<bool> audio_clock_active(false);
    std::atomic<double> audio_clock_base(current_time_.load());
    std::atomic<int> stream_generation(0);

    auto get_audio_clock = [&]() -> double {
        if (!audio_ready || !audio_clock_active.load()) return current_time_.load();
        return audio_clock_base.load() + audio_output_->getPlayedSeconds() * active_speed.load();
    };

    auto is_running = [&]() -> bool {
        return !stop_requested_ && playing_ && session_id_.load() == my_session;
    };

    auto release_frame = [](DecodedFrame& frame) {
        if (frame.data) {
            av_freep(&frame.data);
            frame.data = nullptr;
        }
    };

    std::thread audio_thread;
    if (use_audio_queue) {
        audio_thread = std::thread([&, my_session]() {
            AudioVisualizer fft_calculator;
            fft_calculator.init(64, false);
            bool audio_paused = false;
            AVPacket* packet = av_packet_alloc();
            if (!packet) return;

            auto process_audio_frame = [&](DecodedFrame& frame, int frame_generation) {
                if (!frame.data || frame.data_size <= 0) return;

                std::vector<uint8_t> volume_data(frame.data, frame.data + frame.data_size);
                float current_volume = volume_.load();
                if (current_volume < 1.0f) {
                    int16_t* pcm = (int16_t*)volume_data.data();
                    int sample_count = frame.data_size / 2;
                    for (int i = 0; i < sample_count; i++) {
                        pcm[i] = (int16_t)(pcm[i] * current_volume);
                    }
                }

                bool audio_written = false;
                if (audio_output_->play(volume_data)) {
                    audio_written = true;
                }
                else if (is_running()) {
                    std::cerr << "[Player] 音频写入失败: " << frame.data_size << " bytes" << std::endl;
                }

                if (video_widget_ && show_visualizer_) {
                    fft_calculator.processAudioData(volume_data.data(), frame.data_size,
                        decoder_->getOutputAudioChannels(), decoder_->getOutputAudioSampleRate());
                    video_widget_->setSpectrumData(fft_calculator.getSpectrumData());
                }

                if (audio_written && frame_generation == stream_generation.load()) {
                    if (!audio_clock_active.load()) {
                        audio_clock_base.store(frame.pts);
                        audio_clock_active.store(true);
                    }
                    current_time_ = get_audio_clock();
                }
            };

            while (is_running()) {
                if (paused_) {
                    if (!audio_paused) {
                        audio_output_->pause();
                        audio_paused = true;
                    }
                    std::this_thread::sleep_for(std::chrono::milliseconds(10));
                    continue;
                }

                if (audio_paused) {
                    audio_output_->resume();
                    audio_paused = false;
                }

                if (!audio_packets_.pop(packet, true)) {
                    if (audio_packets_.isFinished() && !audio_packets_.isAborted()) {
                        std::vector<DecodedFrame> frames;
                        {
                            std::lock_guard<std::mutex> lock(seek_mutex_);
                            decoder_->flushAudioDecoder(frames);
                        }
                        int frame_generation = stream_generation.load();
                        for (auto& frame : frames) {
                            if (!is_running() || frame_generation != stream_generation.load()) {
                                release_frame(frame);
                                continue;
                            }
                            process_audio_frame(frame, frame_generation);
                            release_frame(frame);
                        }
                    }
                    break;
                }

                int packet_generation = stream_generation.load();
                std::vector<DecodedFrame> frames;
                {
                    std::lock_guard<std::mutex> lock(seek_mutex_);
                    if (packet_generation == stream_generation.load() && !seek_requested_) {
                        decoder_->decodeAudioPacket(packet, frames);
                    }
                }
                av_packet_unref(packet);

                if (packet_generation != stream_generation.load() || seek_requested_) {
                    for (auto& frame : frames) release_frame(frame);
                    continue;
                }

                for (auto& frame : frames) {
                    if (!is_running() || packet_generation != stream_generation.load()) {
                        release_frame(frame);
                        continue;
                    }
                    process_audio_frame(frame, packet_generation);
                    release_frame(frame);
                }
            }

            av_packet_free(&packet);
        });
    }

    std::thread video_thread;
    if (use_video_queue) {
        video_thread = std::thread([&, my_session]() {
            AVPacket* packet = av_packet_alloc();
            if (!packet) return;

            auto playback_anchor = std::chrono::steady_clock::now();
            double anchor_pts = current_time_.load();
            int local_generation = stream_generation.load();

            auto reset_video_anchor = [&]() {
                playback_anchor = std::chrono::steady_clock::now();
                anchor_pts = current_time_.load();
                local_generation = stream_generation.load();
            };

            auto wait_until_time = [&](std::chrono::steady_clock::time_point target_time) {
                while (is_running() && !paused_ && std::chrono::steady_clock::now() < target_time) {
                    std::this_thread::sleep_for(std::chrono::milliseconds(1));
                }
            };

            auto process_video_frame = [&](DecodedFrame& frame) {
                bool display_frame = true;

                if (audio_ready && audio_clock_active.load()) {
                    constexpr double sync_threshold = 0.04;
                    constexpr double drop_threshold = 0.12;

                    double diff = frame.pts - get_audio_clock();
                    while (is_running() && !paused_ && diff > sync_threshold) {
                        double wait_seconds = std::min(diff, 0.01);
                        std::this_thread::sleep_for(std::chrono::duration<double>(wait_seconds));
                        diff = frame.pts - get_audio_clock();
                    }

                    if (diff < -drop_threshold) {
                        display_frame = false;
                    }
                }
                else if (!audio_ready) {
                    if (local_generation != stream_generation.load()) {
                        reset_video_anchor();
                    }

                    double target_delay = (frame.pts - anchor_pts) / active_speed.load();
                    if (target_delay > 0.0) {
                        auto target_time = playback_anchor + std::chrono::duration_cast<std::chrono::steady_clock::duration>(
                            std::chrono::duration<double>(target_delay));
                        wait_until_time(target_time);
                    }
                }

                if (display_frame && frame.data && video_widget_ && is_running() && !paused_) {
                    video_widget_->setVideoFrame(frame.data, frame.width, frame.height);
                }
                current_time_ = (audio_ready && audio_clock_active.load()) ? get_audio_clock() : frame.pts;
            };

            while (is_running()) {
                if (paused_) {
                    reset_video_anchor();
                    std::this_thread::sleep_for(std::chrono::milliseconds(10));
                    continue;
                }

                if (local_generation != stream_generation.load()) {
                    reset_video_anchor();
                }

                if (!video_packets_.pop(packet, true)) {
                    if (video_packets_.isFinished() && !video_packets_.isAborted()) {
                        std::vector<DecodedFrame> frames;
                        {
                            std::lock_guard<std::mutex> lock(seek_mutex_);
                            decoder_->flushVideoDecoder(frames);
                        }
                        int frame_generation = stream_generation.load();
                        for (auto& frame : frames) {
                            if (!is_running() || frame_generation != stream_generation.load()) {
                                release_frame(frame);
                                continue;
                            }
                            process_video_frame(frame);
                            release_frame(frame);
                        }
                    }
                    break;
                }

                int packet_generation = stream_generation.load();
                std::vector<DecodedFrame> frames;
                {
                    std::lock_guard<std::mutex> lock(seek_mutex_);
                    if (packet_generation == stream_generation.load() && !seek_requested_) {
                        decoder_->decodeVideoPacket(packet, frames);
                    }
                }
                av_packet_unref(packet);

                if (packet_generation != stream_generation.load() || seek_requested_) {
                    for (auto& frame : frames) release_frame(frame);
                    continue;
                }

                for (auto& frame : frames) {
                    if (!is_running() || packet_generation != stream_generation.load()) {
                        release_frame(frame);
                        continue;
                    }
                    process_video_frame(frame);
                    release_frame(frame);
                }
            }

            av_packet_free(&packet);
        });
    }

    AVPacket* packet = av_packet_alloc();
    while (packet && is_running()) {
        if (seek_requested_) {
            double seek_seconds = pending_seek_seconds_.load();
            stream_generation.fetch_add(1);
            audio_packets_.reset();
            video_packets_.reset();
            audio_clock_active.store(false);
            if (audio_ready) {
                audio_output_->reset();
            }
            {
                std::lock_guard<std::mutex> lock(seek_mutex_);
                decoder_->seek(seek_seconds);
                decoder_->setPlaybackSpeed(active_speed.load());
            }
            current_time_ = seek_seconds;
            seek_requested_ = false;
            continue;
        }

        float requested_speed = std::max(0.25f, speed_.load());
        if (std::fabs(requested_speed - active_speed.load()) > 0.001f) {
            double sync_time = audio_clock_active.load() ? get_audio_clock() : current_time_.load();
            stream_generation.fetch_add(1);
            audio_clock_active.store(false);
            if (audio_ready) {
                audio_output_->reset();
            }
            {
                std::lock_guard<std::mutex> lock(seek_mutex_);
                if (!decoder_->setPlaybackSpeed(requested_speed)) {
                    std::cerr << "[Player] 更新播放速度失败: " << requested_speed << "x" << std::endl;
                }
            }
            active_speed.store(requested_speed);
            current_time_ = sync_time;
            continue;
        }

        if (paused_) {
            std::this_thread::sleep_for(std::chrono::milliseconds(10));
            continue;
        }

        if ((use_audio_queue && audio_packets_.size() > 80) ||
            (use_video_queue && video_packets_.size() > 80)) {
            std::this_thread::sleep_for(std::chrono::milliseconds(2));
            continue;
        }

        bool got_packet = false;
        bool is_audio_packet = false;
        bool is_video_packet = false;
        {
            std::lock_guard<std::mutex> lock(seek_mutex_);
            got_packet = decoder_->readPacket(packet);
            if (got_packet) {
                is_audio_packet = decoder_->isAudioPacket(packet);
                is_video_packet = decoder_->isVideoPacket(packet);
            }
        }

        if (!got_packet) {
            break;
        }

        if (is_audio_packet && use_audio_queue) {
            audio_packets_.push(packet);
        }
        else if (is_video_packet && use_video_queue) {
            video_packets_.push(packet);
        }
        av_packet_unref(packet);
    }

    if (packet) {
        av_packet_free(&packet);
    }

    if (stop_requested_ || session_id_.load() != my_session) {
        audio_packets_.abort();
        video_packets_.abort();
    }
    else {
        audio_packets_.finish();
        video_packets_.finish();
    }

    if (audio_thread.joinable()) audio_thread.join();
    if (video_thread.joinable()) video_thread.join();

    if (audio_ready) {
        audio_output_->cleanup();
    }
    playing_ = false;
}
