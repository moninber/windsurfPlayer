/**
 * @file MediaTranscoder.h
 * @brief 媒体转码器 - 基于FFmpeg的音视频格式转换
 * 
 * 转码(Transcoding)是将一种编码格式的媒体文件转换为另一种格式的过程：
 * 
 * 转码流程：
 * 1. 打开源文件（解封装）→ 解码 → 原始帧数据
 * 2. 原始帧数据 → 编码（新格式）→ 封装 → 写入目标文件
 * 
 * 典型应用场景：
 * - 格式转换：MKV → MP4, AVI → MOV
 * - 编码转换：H.264 → H.265(HEVC), AAC → MP3
 * - 分辨率调整：4K → 1080P（视频缩放）
 * - 码率调整：高码率 → 低码率（压缩文件大小）
 * - 音频参数调整：48kHz → 44.1kHz, 5.1ch → 立体声
 * 
 * FFmpeg编码流程（与解码相反）：
 * 1. avformat_alloc_output_context2() → 创建输出格式上下文
 * 2. avcodec_find_encoder()           → 查找编码器
 * 3. avcodec_alloc_context3()         → 创建编码器上下文
 * 4. avcodec_open2()                  → 打开编码器
 * 5. avcodec_send_frame()             → 发送原始帧给编码器
 * 6. avcodec_receive_packet()         → 获取编码后的数据包
 * 7. av_interleaved_write_frame()     → 写入输出文件
 * 
 * 学习要点：
 * - FFmpeg编码API的使用
 * - 视频缩放（sws_scale）
 * - 音频重采样（swr_convert）
 * - 编码参数配置（码率、GOP、预设等）
 */

#pragma once

#include <string>
#include <functional>
#include "MediaInfo.h"

extern "C" {
#include <libavcodec/avcodec.h>
#include <libavformat/avformat.h>
#include <libavutil/imgutils.h>
#include <libswscale/swscale.h>
#include <libswresample/swresample.h>
#include <libavutil/opt.h>
}

class MediaTranscoder {
public:
    MediaTranscoder();
    ~MediaTranscoder();

    /**
     * @brief 执行转码
     * @param input_path 输入文件路径
     * @param output_path 输出文件路径
     * @param target_width 目标宽度（0=保持原始）
     * @param target_height 目标高度（0=保持原始）
     * @param video_codec 目标视频编码（如"libx264", "libx265"）
     * @param audio_codec 目标音频编码（如"aac", "libmp3lame"）
     * @param bitrate 目标视频码率（0=自动）
     * @param progress_cb 进度回调函数（参数：0.0~1.0进度百分比）
     * @return true=成功, false=失败
     */
    bool transcode(
        const std::string& input_path,
        const std::string& output_path,
        int target_width = 0,
        int target_height = 0,
        const std::string& video_codec = "libx264",
        const std::string& audio_codec = "aac",
        int bitrate = 0,
        std::function<void(float)> progress_cb = nullptr
    );

    /** @brief 取消正在进行的转码 */
    void cancel();

    /** @brief 获取最后的错误信息 */
    const std::string& getLastError() const { return last_error_; }

private:
    /**
     * @brief 处理视频流转码
     * 解码 → 缩放 → 编码 → 写入
     */
    bool processVideoStream(
        AVFormatContext* out_fmt_ctx,
        AVCodecContext* dec_ctx,
        AVCodecContext* enc_ctx,
        AVRational in_time_base,
        int out_stream_idx,
        AVPacket* packet,
        AVFrame* frame,
        SwsContext** sws_ctx,
        int64_t total_duration,
        std::function<void(float)>& progress_cb
    );

    /**
     * @brief 处理音频流转码
     * 解码 → 重采样 → 编码 → 写入
     */
    bool processAudioStream(
        AVFormatContext* out_fmt_ctx,
        AVCodecContext* dec_ctx,
        AVCodecContext* enc_ctx,
        AVRational in_time_base,
        int out_stream_idx,
        AVPacket* packet,
        AVFrame* frame,
        SwrContext** swr_ctx,
        int64_t total_duration,
        std::function<void(float)>& progress_cb
    );

    bool cancelled_;        // 取消标志
    std::string last_error_; // 最后错误信息
};
