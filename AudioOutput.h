/**
 * @file AudioOutput.h
 * @brief 音频输出 - 基于Windows WASAPI的音频播放
 * 
 * WASAPI (Windows Audio Session API) 是Windows Vista及之后版本的核心音频API：
 * - 低延迟音频输出
 * - 共享模式/独占模式
 * - 支持多种音频格式
 * 
 * WASAPI播放流程：
 * 1. 获取默认音频端点设备（IMMDevice）
 * 2. 激活音频客户端（IAudioClient）
 * 3. 设置音频格式（WAVEFORMATEX）
 * 4. 初始化音频客户端（分配缓冲区）
 * 5. 获取渲染客户端（IAudioRenderClient）
 * 6. 循环：请求缓冲区 → 写入PCM数据 → 释放缓冲区
 * 
 * COM (Component Object Model) 说明：
 * - WASAPI基于COM，使用前需调用CoInitializeEx()
 * - COM对象使用Release()释放（类似智能指针的引用计数）
 * - COM接口使用HRESULT返回状态码
 */

#pragma once

#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#define NOMINMAX  // 禁用min/max宏，避免与std::min/std::max冲突

#include <windows.h>
#include <mmdeviceapi.h>    // IMMDeviceEnumerator, IMMDevice
#include <audioclient.h>    // IAudioClient, IAudioRenderClient
#include <vector>
#include <atomic>
#include <mutex>

class AudioOutput {
public:
    AudioOutput();
    ~AudioOutput();

    /**
     * @brief 初始化音频输出设备
     * @param sample_rate 采样率（Hz）
     * @param channels 声道数
     * @return true=成功, false=失败
     */
    bool init(int sample_rate, int channels);

    /** @brief 释放WASAPI资源 */
    void cleanup();

    /**
     * @brief 播放一段PCM音频数据
     * @param data PCM音频数据（16位有符号整数格式）
     * @return true=成功, false=失败
     * 
     * 工作流程：
     * 1. 查询缓冲区可用空间
     * 2. 请求写入缓冲区
     * 3. 拷贝PCM数据到缓冲区
     * 4. 释放缓冲区（提交数据）
     */
    bool play(const std::vector<uint8_t>& data);

    /** @brief 设置音量（0.0~1.0） */
    void setVolume(float volume);

    /** @brief 获取当前音量 */
    float getVolume() const { return volume_.load(); }

    /** @brief 暂停播放 */
    void pause();

    /** @brief 恢复播放 */
    void resume();

    /** @brief 是否正在播放 */
    bool isPlaying() const { return playing_.load(); }

private:
    // WASAPI COM接口指针
    IMMDeviceEnumerator* device_enumerator_;  // 设备枚举器（用于查找音频设备）
    IMMDevice* device_;                       // 音频端点设备（扬声器/耳机）
    IAudioClient* audio_client_;              // 音频客户端（管理音频流）
    IAudioRenderClient* render_client_;       // 渲染客户端（写入PCM数据）

    // 音频格式参数
    int sample_rate_;                          // 采样率
    int channels_;                             // 声道数
    int bytes_per_sample_;                     // 每采样字节数（16位=2字节）
    WAVEFORMATEX* wave_format_;               // 音频格式描述结构

    // 状态控制（使用原子变量，线程安全）
    std::atomic<float> volume_;                // 音量
    std::atomic<bool> playing_;                // 是否正在播放
    bool com_initialized_;                     // 是否由本对象初始化了COM
    UINT32 buffer_frame_count_;               // WASAPI缓冲区帧数（play()中限速用）
    std::mutex mutex_;                         // 互斥锁（保护WASAPI调用）

    HANDLE audio_event_;                       // 音频事件句柄（用于缓冲区通知）
};
