/**
 * @file AudioOutput.cpp
 * @brief 音频输出实现 - WASAPI音频播放的具体实现
 */

#include "AudioOutput.h"
#include <iostream>
#include <thread>
#include <chrono>
#include <mmreg.h>      // WAVE_FORMAT_PCM等常量定义
#include <algorithm>    // std::min, std::max

AudioOutput::AudioOutput()
    : device_enumerator_(nullptr)
    , device_(nullptr)
    , audio_client_(nullptr)
    , render_client_(nullptr)
    , sample_rate_(0)
    , channels_(0)
    , bytes_per_sample_(2)  // 16位PCM = 2字节
    , wave_format_(nullptr)
    , volume_(1.0f)
    , playing_(false)
    , com_initialized_(false)
    , buffer_frame_count_(0)
    , audio_event_(nullptr)
{
}

AudioOutput::~AudioOutput()
{
    cleanup();
}

bool AudioOutput::init(int sample_rate, int channels)
{
    cleanup();

    sample_rate_ = sample_rate;
    channels_ = channels;

    // 后台线程需要自己初始化COM（WASAPI是COM对象）
    // COINIT_MULTITHREADED适合后台工作线程
    HRESULT com_hr = CoInitializeEx(nullptr, COINIT_MULTITHREADED);
    com_initialized_ = SUCCEEDED(com_hr) || com_hr == S_FALSE;

    // --------------------------------------------------------
    // 步骤1：创建设备枚举器
    // MMDeviceEnumerator是WASAPI的入口点
    // 通过它可以枚举系统中的所有音频设备
    // --------------------------------------------------------
    HRESULT hr = CoCreateInstance(
        __uuidof(MMDeviceEnumerator),  // COM类ID
        nullptr,                        // 无外部未知
        CLSCTX_ALL,                     // 所有执行上下文
        __uuidof(IMMDeviceEnumerator),  // 接口ID
        (void**)&device_enumerator_     // 输出接口指针
    );
    if (FAILED(hr)) {
        std::cerr << "[AudioOutput] 创建设备枚举器失败" << std::endl;
        return false;
    }

    // --------------------------------------------------------
    // 步骤2：获取默认音频渲染端点
    // eRender = 音频输出设备（扬声器/耳机）
    // eConsole = 控制台应用程序角色
    // --------------------------------------------------------
    hr = device_enumerator_->GetDefaultAudioEndpoint(eRender, eConsole, &device_);
    if (FAILED(hr)) {
        std::cerr << "[AudioOutput] 获取默认音频端点失败" << std::endl;
        return false;
    }

    // --------------------------------------------------------
    // 步骤3：激活音频客户端
    // IAudioClient是WASAPI的核心接口
    // 它管理音频流的格式、缓冲区和状态
    // --------------------------------------------------------
    hr = device_->Activate(__uuidof(IAudioClient), CLSCTX_ALL, nullptr, (void**)&audio_client_);
    if (FAILED(hr)) {
        std::cerr << "[AudioOutput] 激活音频客户端失败" << std::endl;
        return false;
    }

    // --------------------------------------------------------
    // 步骤4：设置音频格式
    // WAVEFORMATEX描述PCM音频格式：
    // - wFormatTag = WAVE_FORMAT_PCM (未压缩PCM)
    // - nChannels = 声道数
    // - nSamplesPerSec = 采样率
    // - wBitsPerSample = 位深度（16位）
    // - nBlockAlign = 每样本字节数 = (wBitsPerSample/8) * nChannels
    // - nAvgBytesPerSec = 每秒字节数 = nSamplesPerSec * nBlockAlign
    // --------------------------------------------------------
    WAVEFORMATEX desired_format = {};
    desired_format.wFormatTag = WAVE_FORMAT_PCM;
    desired_format.nChannels = channels_;
    desired_format.nSamplesPerSec = sample_rate_;
    desired_format.wBitsPerSample = 16;
    desired_format.nBlockAlign = (desired_format.wBitsPerSample / 8) * desired_format.nChannels;
    desired_format.nAvgBytesPerSec = desired_format.nSamplesPerSec * desired_format.nBlockAlign;
    desired_format.cbSize = 0;

    // 检查格式是否被设备支持
    // WASAPI在共享模式下可能需要使用最接近的匹配格式
    WAVEFORMATEX* closest_match = nullptr;
    hr = audio_client_->IsFormatSupported(AUDCLNT_SHAREMODE_SHARED, &desired_format, &closest_match);

    if (hr == S_FALSE && closest_match) {
        wave_format_ = (WAVEFORMATEX*)CoTaskMemAlloc(sizeof(WAVEFORMATEX));
        if (!wave_format_) {
            CoTaskMemFree(closest_match);
            return false;
        }
        *wave_format_ = desired_format;
        CoTaskMemFree(closest_match);
    }
    else if (hr == S_OK) {
        // 请求的格式完全支持
        wave_format_ = (WAVEFORMATEX*)CoTaskMemAlloc(sizeof(WAVEFORMATEX));
        if (!wave_format_) {
            return false;
        }
        *wave_format_ = desired_format;
    }
    else {
        std::cerr << "[AudioOutput] 音频格式不支持" << std::endl;
        if (closest_match) CoTaskMemFree(closest_match);
        return false;
    }

    // --------------------------------------------------------
    // 步骤5：初始化音频客户端
    // 分配音频缓冲区，指定缓冲持续时间
    // REFERENCE_TIME单位：100纳秒（1秒 = 10,000,000 × 100ns）
    // --------------------------------------------------------
    REFERENCE_TIME buffer_duration = 2000000; // 0.2秒缓冲（短缓冲才能精确限速）
    DWORD stream_flags = AUDCLNT_STREAMFLAGS_AUTOCONVERTPCM | AUDCLNT_STREAMFLAGS_SRC_DEFAULT_QUALITY;
    hr = audio_client_->Initialize(
        AUDCLNT_SHAREMODE_SHARED,  // 共享模式（与其他应用共享设备）
        stream_flags,               // 允许音频引擎自动转换到设备混音格式
        buffer_duration,            // 缓冲区持续时间
        0,                          // 设备周期（共享模式设0）
        wave_format_,               // 音频格式
        nullptr                     // 无事件会话
    );
    if (FAILED(hr)) {
        std::cerr << "[AudioOutput] 初始化音频客户端失败 (hr=0x"
                  << std::hex << hr << std::dec << ")" << std::endl;
        return false;
    }

    // --------------------------------------------------------
    // 步骤6：获取渲染客户端接口
    // IAudioRenderClient用于向缓冲区写入PCM数据
    // --------------------------------------------------------
    hr = audio_client_->GetService(__uuidof(IAudioRenderClient), (void**)&render_client_);
    if (FAILED(hr)) {
        std::cerr << "[AudioOutput] 获取渲染客户端失败" << std::endl;
        return false;
    }

    bytes_per_sample_ = wave_format_->wBitsPerSample / 8;

    // 获取实际分配的缓冲区大小
    hr = audio_client_->GetBufferSize(&buffer_frame_count_);
    if (FAILED(hr)) buffer_frame_count_ = 0;

    std::cout << "[AudioOutput] 初始化成功: " << sample_rate_ << "Hz, "
              << channels_ << "ch, 16bit, buf=" << buffer_frame_count_ << "frames" << std::endl;
    return true;
}

void AudioOutput::cleanup()
{
    // 停止播放
    if (audio_client_) {
        audio_client_->Stop();
    }

    // 释放COM接口（调用Release()减少引用计数）
    // 释放顺序：后获取的先释放
    if (render_client_) {
        render_client_->Release();
        render_client_ = nullptr;
    }
    if (audio_client_) {
        audio_client_->Release();
        audio_client_ = nullptr;
    }
    if (device_) {
        device_->Release();
        device_ = nullptr;
    }
    if (device_enumerator_) {
        device_enumerator_->Release();
        device_enumerator_ = nullptr;
    }

    // 释放WAVEFORMATEX（使用COM分配器分配的内存）
    if (wave_format_) {
        CoTaskMemFree(wave_format_);
        wave_format_ = nullptr;
    }

    if (audio_event_) {
        CloseHandle(audio_event_);
        audio_event_ = nullptr;
    }

    // 平衡COM初始化
    if (com_initialized_) {
        CoUninitialize();
        com_initialized_ = false;
    }
}

bool AudioOutput::play(const std::vector<uint8_t>& data)
{
    if (!wave_format_) return false;

    UINT32 total_frames = (UINT32)(data.size() / wave_format_->nBlockAlign);
    if (total_frames == 0) return true;

    UINT32 written_frames = 0;
    int idle_retry = 0;

    while (written_frames < total_frames) {
        {
            std::lock_guard<std::mutex> lock(mutex_);
            if (!audio_client_ || !render_client_) return false;

            UINT32 num_padding_frames = 0;
            HRESULT hr = audio_client_->GetCurrentPadding(&num_padding_frames);
            if (FAILED(hr)) return false;

            UINT32 num_available = (buffer_frame_count_ > num_padding_frames)
                ? (buffer_frame_count_ - num_padding_frames)
                : 0;
            UINT32 remaining_frames = total_frames - written_frames;
            UINT32 to_write = std::min(num_available, remaining_frames);

            if (to_write > 0) {
                BYTE* data_ptr = nullptr;
                hr = render_client_->GetBuffer(to_write, &data_ptr);
                if (FAILED(hr)) return false;

                const uint8_t* src = data.data() + written_frames * wave_format_->nBlockAlign;
                memcpy(data_ptr, src, to_write * wave_format_->nBlockAlign);

                hr = render_client_->ReleaseBuffer(to_write, 0);
                if (FAILED(hr)) return false;

                if (!playing_) {
                    hr = audio_client_->Start();
                    if (FAILED(hr)) return false;
                    playing_.store(true);
                }

                written_frames += to_write;
                idle_retry = 0;
                continue;
            }
        }

        if (++idle_retry > 200) {
            return false;
        }

        std::this_thread::sleep_for(std::chrono::milliseconds(5));
    }

    return true;
}

void AudioOutput::setVolume(float volume)
{
    volume_.store(std::max(0.0f, std::min(1.0f, volume)));
}

void AudioOutput::pause()
{
    std::lock_guard<std::mutex> lock(mutex_);
    if (audio_client_ && playing_) {
        audio_client_->Stop();
        playing_.store(false);
    }
}

void AudioOutput::resume()
{
    std::lock_guard<std::mutex> lock(mutex_);
    if (audio_client_ && !playing_) {
        audio_client_->Start();
        playing_ = true;
    }
}
