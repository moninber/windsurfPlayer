/**
 * @file MediaDecoder.cpp
 * @brief 媒体解码器实现 - FFmpeg解封装与解码的具体实现
 */

#include "MediaDecoder.h"
#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <iostream>
#include <sstream>

// 外部调用者（PlayerController）
// │
// ├── open(filename)
// │   ├── avformat_open_input()       打开文件
// │   ├── avformat_find_stream_info() 探测流
// │   ├── avcodec_find_decoder()      查找解码器
// │   ├── avcodec_open2()             打开解码器
// │   └── extractMediaInfo()          填充 media_info_
// │
// ├── setPlaybackSpeed(speed)
// │   ├── destroyAudioFilterGraph()   销毁旧滤镜图
// │   ├── buildAtempoFilterChain()    构造 atempo 滤镜字符串
// │   │   └── 返回 "atempo=2.0" 等字符串
// │   └── initAudioFilterGraph()      重建滤镜图（变速不变调）
// │
// ├── seek(seconds)
// │   ├── av_seek_frame()             跳转到关键帧
// │   ├── avcodec_flush_buffers()     清空解码器缓冲
// │   └── 重置 eof/flush 状态标志
// │
// └── close()
//     ├── destroyAudioFilterGraph()
//     ├── avcodec_free_context()      释放视频/音频解码器
//     ├── sws_freeContext()
//     ├── swr_free()
//     └── avformat_close_input()

// ============================================================
// 构造函数：初始化所有指针为nullptr
// ============================================================
MediaDecoder::MediaDecoder()
    : format_ctx_(nullptr)
    , video_codec_ctx_(nullptr)
    , audio_codec_ctx_(nullptr)
    , sws_ctx_(nullptr)
    , swr_ctx_(nullptr)
    , audio_filter_graph_(nullptr)
    , audio_buffer_src_ctx_(nullptr)
    , audio_buffer_sink_ctx_(nullptr)
    , video_stream_index_(-1)
    , audio_stream_index_(-1)
    , current_time_(0.0)
    , playback_speed_(1.0f)
    , audio_drained_(false)
    , audio_filter_flush_sent_(false)
    , last_video_pts_(0.0)
    , has_last_video_pts_(false)
{
}

// ============================================================
// 析构函数：确保资源释放
// ============================================================
MediaDecoder::~MediaDecoder()
{
    close();
}

// ============================================================
// 打开媒体文件
// ============================================================
bool MediaDecoder::open(const std::string& filename)
{
    close(); // 先关闭之前打开的文件

    // --------------------------------------------------------
    // 步骤1：打开文件，创建AVFormatContext
    // avformat_open_input() 会：
    // - 读取文件头信息
    // - 创建并填充format_ctx_
    // - 不会读取帧数据（需要avformat_find_stream_info进一步探测）
    // --------------------------------------------------------
    if (avformat_open_input(&format_ctx_, filename.c_str(), nullptr, nullptr) != 0) {
        std::cerr << "[MediaDecoder] 无法打开文件: " << filename << std::endl;
        return false;
    }

    // --------------------------------------------------------
    // 步骤2：探测流信息
    // avformat_find_stream_info() 会：
    // - 读取若干帧数据来探测编码参数
    // - 填充format_ctx_->streams[i]->codecpar
    // - 对于某些格式（如MKV），这是获取完整信息的必要步骤
    // --------------------------------------------------------
    if (avformat_find_stream_info(format_ctx_, nullptr) < 0) {
        std::cerr << "[MediaDecoder] 无法获取流信息" << std::endl;
        close();
        return false;
    }

    // --------------------------------------------------------
    // 步骤3：查找视频流和音频流
    // 一个媒体文件可能包含多个流（视频、音频、字幕等）
    // 我们需要找到第一个视频流和第一个音频流
    // --------------------------------------------------------
    video_stream_index_ = -1;
    audio_stream_index_ = -1;

    for (unsigned int i = 0; i < format_ctx_->nb_streams; i++) {
        AVCodecParameters* codec_params = format_ctx_->streams[i]->codecpar;

        if (codec_params->codec_type == AVMEDIA_TYPE_VIDEO && video_stream_index_ == -1) {
            video_stream_index_ = i;
        }
        else if (codec_params->codec_type == AVMEDIA_TYPE_AUDIO && audio_stream_index_ == -1) {
            audio_stream_index_ = i;
        }
    }

    // --------------------------------------------------------
    // 步骤4：打开视频解码器
    // 流程：查找解码器 → 创建上下文 → 复制参数 → 打开解码器
    // --------------------------------------------------------
    if (video_stream_index_ != -1) {
        AVCodecParameters* codec_params = format_ctx_->streams[video_stream_index_]->codecpar;
        // avcodec_find_decoder() 根据codec_id查找已注册的解码器
        const AVCodec* video_codec = avcodec_find_decoder(codec_params->codec_id);
        if (!video_codec) {
            std::cerr << "[MediaDecoder] 不支持的视频编码器" << std::endl;
            close();
            return false;
        }

        // 创建解码器上下文并复制流参数
        video_codec_ctx_ = avcodec_alloc_context3(video_codec);
        if (avcodec_parameters_to_context(video_codec_ctx_, codec_params) < 0) {
            std::cerr << "[MediaDecoder] 无法复制视频编解码器参数" << std::endl;
            close();
            return false;
        }

        // 打开解码器（初始化解码器内部状态）
        if (avcodec_open2(video_codec_ctx_, video_codec, nullptr) < 0) {
            std::cerr << "[MediaDecoder] 无法打开视频解码器" << std::endl;
            close();
            return false;
        }

        media_info_.has_video = true;
    }

    // --------------------------------------------------------
    // 步骤5：打开音频解码器（流程同视频）
    // --------------------------------------------------------
    if (audio_stream_index_ != -1) {
        AVCodecParameters* codec_params = format_ctx_->streams[audio_stream_index_]->codecpar;
        const AVCodec* audio_codec = avcodec_find_decoder(codec_params->codec_id);
        if (!audio_codec) {
            std::cerr << "[MediaDecoder] 不支持的音频编码器" << std::endl;
            // 音频解码失败不终止，可以只播放视频
        }
        else {
            audio_codec_ctx_ = avcodec_alloc_context3(audio_codec);
            avcodec_parameters_to_context(audio_codec_ctx_, codec_params);
            if (avcodec_open2(audio_codec_ctx_, audio_codec, nullptr) < 0) {
                std::cerr << "[MediaDecoder] 无法打开音频解码器" << std::endl;
                avcodec_free_context(&audio_codec_ctx_);
                audio_codec_ctx_ = nullptr;
            }
            else {
                media_info_.has_audio = true;

                // ------------------------------------------------
                // 步骤6：初始化音频重采样器（SwrContext）
                // 为什么需要重采样？
                // - 不同音频源使用不同的采样率/格式/声道布局
                // - 音频输出设备通常要求固定的参数
                // - 我们统一转换为：44100Hz, 立体声, 16位PCM(S16)
                // ------------------------------------------------
                AVChannelLayout dst_ch_layout;
                av_channel_layout_default(&dst_ch_layout, TARGET_CHANNELS);

                int ret = swr_alloc_set_opts2(&swr_ctx_,
                    &dst_ch_layout,                    // 目标声道布局
                    TARGET_SAMPLE_FMT,                 // 目标采样格式(S16)
                    TARGET_SAMPLE_RATE,                // 目标采样率
                    &audio_codec_ctx_->ch_layout,      // 源声道布局
                    audio_codec_ctx_->sample_fmt,      // 源采样格式
                    audio_codec_ctx_->sample_rate,     // 源采样率
                    0, nullptr);

                if (ret >= 0 && swr_ctx_) {
                    ret = swr_init(swr_ctx_);
                    if (ret >= 0) {
                        if (!initAudioFilterGraph()) {
                            std::cerr << "[MediaDecoder] 初始化音频tempo滤镜失败" << std::endl;
                            destroyAudioFilterGraph();
                            swr_free(&swr_ctx_);
                            avcodec_free_context(&audio_codec_ctx_);
                            media_info_.has_audio = false;
                            audio_stream_index_ = -1;
                        }
                    }
                    else {
                        swr_free(&swr_ctx_);
                        avcodec_free_context(&audio_codec_ctx_);
                        media_info_.has_audio = false;
                        audio_stream_index_ = -1;
                    }
                }
                else {
                    swr_free(&swr_ctx_);
                    avcodec_free_context(&audio_codec_ctx_);
                    media_info_.has_audio = false;
                    audio_stream_index_ = -1;
                }
                av_channel_layout_uninit(&dst_ch_layout);
            }
        }
    }

    // 提取媒体信息
    media_info_.file_path = filename;
    // 从路径中提取文件名
    size_t pos = filename.find_last_of("/\\");
    media_info_.file_name = (pos != std::string::npos) ? filename.substr(pos + 1) : filename;
    audio_drained_ = !media_info_.has_audio;
    audio_filter_flush_sent_ = false;
    extractMediaInfo();

    std::cout << "[MediaDecoder] 文件打开成功: " << filename << std::endl;
    if (media_info_.has_video) {
        std::cout << "  视频: " << media_info_.video_info.width << "x"
                  << media_info_.video_info.height << " @ "
                  << media_info_.video_info.frame_rate << "fps, "
                  << media_info_.video_info.codec_name << std::endl;
    }
    if (media_info_.has_audio) {
        std::cout << "  音频: " << media_info_.audio_info.sample_rate << "Hz, "
                  << media_info_.audio_info.channels << "ch, "
                  << media_info_.audio_info.codec_name << std::endl;
    }

    return true;
}

// ============================================================
// 关闭文件并释放所有资源
// ============================================================
void MediaDecoder::close()
{
    // 释放顺序：后创建的先释放（与创建顺序相反）

    destroyAudioFilterGraph();

    if (swr_ctx_) {
        swr_free(&swr_ctx_);         // 释放音频重采样器
        swr_ctx_ = nullptr;
    }

    if (sws_ctx_) {
        sws_freeContext(sws_ctx_);    // 释放图像转换器
        sws_ctx_ = nullptr;
    }

    if (audio_codec_ctx_) {
        avcodec_free_context(&audio_codec_ctx_);  // 释放音频解码器上下文
        audio_codec_ctx_ = nullptr;
    }

    if (video_codec_ctx_) {
        avcodec_free_context(&video_codec_ctx_);  // 释放视频解码器上下文
        video_codec_ctx_ = nullptr;
    }

    if (format_ctx_) {
        avformat_close_input(&format_ctx_);        // 关闭文件并释放格式上下文
        format_ctx_ = nullptr;
    }

    video_stream_index_ = -1;
    audio_stream_index_ = -1;
    current_time_ = 0.0;
    playback_speed_ = 1.0f;
    media_info_ = MediaInfo();  // 重置媒体信息
    audio_drained_ = false;
    audio_filter_flush_sent_ = false;
    last_video_pts_ = 0.0;
    has_last_video_pts_ = false;
}

bool MediaDecoder::setPlaybackSpeed(float speed)
{
    float clamped_speed = std::max(0.25f, std::min(4.0f, speed));
    if (std::fabs(playback_speed_ - clamped_speed) < 0.001f) {
        return true;
    }

    playback_speed_ = clamped_speed;

    if (!media_info_.has_audio || !audio_codec_ctx_ || !swr_ctx_) {
        return true;
    }

    swr_close(swr_ctx_);
    if (swr_init(swr_ctx_) < 0) {
        std::cerr << "[MediaDecoder] 重新初始化音频重采样器失败" << std::endl;
        return false;
    }

    audio_drained_ = false;
    audio_filter_flush_sent_ = false;
    return initAudioFilterGraph();
}

bool MediaDecoder::initAudioFilterGraph()
{
    if (!audio_codec_ctx_ || !media_info_.has_audio) {
        return false;
    }

    destroyAudioFilterGraph();

    audio_filter_graph_ = avfilter_graph_alloc();
    if (!audio_filter_graph_) {
        return false;
    }

    const AVFilter* abuffer = avfilter_get_by_name("abuffer");
    const AVFilter* abuffersink = avfilter_get_by_name("abuffersink");
    if (!abuffer || !abuffersink) {
        destroyAudioFilterGraph();
        return false;
    }

    AVChannelLayout output_layout;
    av_channel_layout_default(&output_layout, TARGET_CHANNELS);

    char layout_desc[64] = {};
    if (av_channel_layout_describe(&output_layout, layout_desc, sizeof(layout_desc)) < 0) {
        av_channel_layout_uninit(&output_layout);
        destroyAudioFilterGraph();
        return false;
    }

    char src_args[256] = {};
    snprintf(src_args, sizeof(src_args),
        "time_base=1/%d:sample_rate=%d:sample_fmt=%s:channel_layout=%s",
        TARGET_SAMPLE_RATE,
        TARGET_SAMPLE_RATE,
        av_get_sample_fmt_name(TARGET_SAMPLE_FMT),
        layout_desc);

    int ret = avfilter_graph_create_filter(&audio_buffer_src_ctx_, abuffer, "in",
        src_args, nullptr, audio_filter_graph_);
    if (ret < 0) {
        av_channel_layout_uninit(&output_layout);
        destroyAudioFilterGraph();
        return false;
    }

    ret = avfilter_graph_create_filter(&audio_buffer_sink_ctx_, abuffersink, "out",
        nullptr, nullptr, audio_filter_graph_);
    if (ret < 0) {
        av_channel_layout_uninit(&output_layout);
        destroyAudioFilterGraph();
        return false;
    }

    std::string filter_desc = buildAtempoFilterChain(playback_speed_);
    filter_desc += ",aformat=sample_fmts=s16:sample_rates=44100:channel_layouts=stereo";

    AVFilterInOut* inputs = avfilter_inout_alloc();
    AVFilterInOut* outputs = avfilter_inout_alloc();
    if (!inputs || !outputs) {
        avfilter_inout_free(&inputs);
        avfilter_inout_free(&outputs);
        av_channel_layout_uninit(&output_layout);
        destroyAudioFilterGraph();
        return false;
    }

    outputs->name = av_strdup("in");
    outputs->filter_ctx = audio_buffer_src_ctx_;
    outputs->pad_idx = 0;
    outputs->next = nullptr;

    inputs->name = av_strdup("out");
    inputs->filter_ctx = audio_buffer_sink_ctx_;
    inputs->pad_idx = 0;
    inputs->next = nullptr;

    ret = avfilter_graph_parse_ptr(audio_filter_graph_, filter_desc.c_str(), &inputs, &outputs, nullptr);
    avfilter_inout_free(&inputs);
    avfilter_inout_free(&outputs);
    av_channel_layout_uninit(&output_layout);
    if (ret < 0) {
        destroyAudioFilterGraph();
        return false;
    }

    ret = avfilter_graph_config(audio_filter_graph_, nullptr);
    if (ret < 0) {
        destroyAudioFilterGraph();
        return false;
    }

    audio_filter_flush_sent_ = false;
    return true;
}

void MediaDecoder::destroyAudioFilterGraph()
{
    if (audio_filter_graph_) {
        avfilter_graph_free(&audio_filter_graph_);
        audio_filter_graph_ = nullptr;
    }
    audio_buffer_src_ctx_ = nullptr;
    audio_buffer_sink_ctx_ = nullptr;
    audio_filter_flush_sent_ = false;
}

std::string MediaDecoder::buildAtempoFilterChain(float speed) const
{
    double remaining = std::max(0.25f, std::min(4.0f, speed));
    std::ostringstream filter_chain;
    filter_chain.setf(std::ios::fixed);
    filter_chain.precision(6);

    bool first_stage = true;
    while (remaining < 0.5 - 0.0001) {
        if (!first_stage) filter_chain << ",";
        filter_chain << "atempo=0.5";
        remaining /= 0.5;
        first_stage = false;
    }
    while (remaining > 2.0 + 0.0001) {
        if (!first_stage) filter_chain << ",";
        filter_chain << "atempo=2.0";
        remaining /= 2.0;
        first_stage = false;
    }

    if (!first_stage) filter_chain << ",";
    filter_chain << "atempo=" << remaining;
    return filter_chain.str();
}

bool MediaDecoder::pullAudioFilterFrame(DecodedFrame& out_frame)
{
    if (!audio_buffer_sink_ctx_ || audio_drained_) {
        return false;
    }

    AVFrame* filtered_frame = av_frame_alloc();
    if (!filtered_frame) {
        return false;
    }

    int ret = av_buffersink_get_frame(audio_buffer_sink_ctx_, filtered_frame);
    if (ret == AVERROR(EAGAIN)) {
        av_frame_free(&filtered_frame);
        return false;
    }
    if (ret == AVERROR_EOF) {
        audio_drained_ = true;
        av_frame_free(&filtered_frame);
        return false;
    }
    if (ret < 0) {
        av_frame_free(&filtered_frame);
        return false;
    }

    int data_size = av_samples_get_buffer_size(nullptr, TARGET_CHANNELS,
        filtered_frame->nb_samples, TARGET_SAMPLE_FMT, 1);
    if (data_size <= 0) {
        av_frame_free(&filtered_frame);
        return false;
    }

    uint8_t* output_buffer = (uint8_t*)av_malloc(data_size);
    if (!output_buffer) {
        av_frame_free(&filtered_frame);
        return false;
    }

    memcpy(output_buffer, filtered_frame->data[0], data_size);

    out_frame.type = DecodedFrame::Type::Audio;
    out_frame.data = output_buffer;
    out_frame.samples = filtered_frame->nb_samples;
    out_frame.data_size = data_size;
    out_frame.pts = (filtered_frame->pts != AV_NOPTS_VALUE)
        ? (double)filtered_frame->pts / TARGET_SAMPLE_RATE
        : current_time_.load();
    current_time_ = out_frame.pts;

    av_frame_free(&filtered_frame);
    return true;
}

bool MediaDecoder::readPacket(AVPacket* packet)
{
    if (!format_ctx_ || !packet) {
        return false;
    }

    av_packet_unref(packet);
    while (true) {
        int ret = av_read_frame(format_ctx_, packet);
        if (ret < 0) {
            return false;
        }
        if (isVideoPacket(packet) || isAudioPacket(packet)) {
            return true;
        }
        av_packet_unref(packet);
    }
}

bool MediaDecoder::isVideoPacket(const AVPacket* packet) const
{
    return packet && video_codec_ctx_ && packet->stream_index == video_stream_index_;
}

bool MediaDecoder::isAudioPacket(const AVPacket* packet) const
{
    return packet && audio_codec_ctx_ && packet->stream_index == audio_stream_index_;
}

bool MediaDecoder::decodeVideoPacket(const AVPacket* packet, std::vector<DecodedFrame>& out_frames)
{
    if (!video_codec_ctx_ || !packet) {
        return false;
    }

    int ret = avcodec_send_packet(video_codec_ctx_, packet);
    if (ret < 0 && ret != AVERROR(EAGAIN) && ret != AVERROR_EOF) {
        return false;
    }

    receiveVideoFrames(out_frames);
    return true;
}

bool MediaDecoder::decodeAudioPacket(const AVPacket* packet, std::vector<DecodedFrame>& out_frames)
{
    if (!audio_codec_ctx_ || !packet) {
        return false;
    }

    DecodedFrame filtered_frame;
    while (pullAudioFilterFrame(filtered_frame)) {
        out_frames.push_back(filtered_frame);
        filtered_frame = DecodedFrame();
    }

    int ret = avcodec_send_packet(audio_codec_ctx_, packet);
    if (ret < 0 && ret != AVERROR(EAGAIN) && ret != AVERROR_EOF) {
        return false;
    }

    receiveAudioFrames(out_frames);
    while (pullAudioFilterFrame(filtered_frame)) {
        out_frames.push_back(filtered_frame);
        filtered_frame = DecodedFrame();
    }
    return true;
}

bool MediaDecoder::flushVideoDecoder(std::vector<DecodedFrame>& out_frames)
{
    if (!video_codec_ctx_) {
        return false;
    }

    avcodec_send_packet(video_codec_ctx_, nullptr);
    receiveVideoFrames(out_frames);
    return true;
}

bool MediaDecoder::flushAudioDecoder(std::vector<DecodedFrame>& out_frames)
{
    if (!audio_codec_ctx_) {
        return false;
    }

    DecodedFrame filtered_frame;
    while (pullAudioFilterFrame(filtered_frame)) {
        out_frames.push_back(filtered_frame);
        filtered_frame = DecodedFrame();
    }

    avcodec_send_packet(audio_codec_ctx_, nullptr);
    receiveAudioFrames(out_frames);

    if (audio_buffer_src_ctx_ && !audio_filter_flush_sent_) {
        av_buffersrc_add_frame_flags(audio_buffer_src_ctx_, nullptr, 0);
        audio_filter_flush_sent_ = true;
    }
    while (pullAudioFilterFrame(filtered_frame)) {
        out_frames.push_back(filtered_frame);
        filtered_frame = DecodedFrame();
    }

    audio_drained_ = true;
    return true;
}

void MediaDecoder::receiveVideoFrames(std::vector<DecodedFrame>& out_frames)
{
    if (!video_codec_ctx_) {
        return;
    }

    AVFrame* frame = av_frame_alloc();
    if (!frame) {
        return;
    }

    while (true) {
        int ret = avcodec_receive_frame(video_codec_ctx_, frame);
        if (ret == 0) {
            DecodedFrame out_frame;
            if (convertVideoFrame(frame, out_frame)) {
                out_frames.push_back(out_frame);
            }
            av_frame_unref(frame);
            continue;
        }
        break;
    }

    av_frame_free(&frame);
}

void MediaDecoder::receiveAudioFrames(std::vector<DecodedFrame>& out_frames)
{
    if (!audio_codec_ctx_) {
        return;
    }

    AVFrame* frame = av_frame_alloc();
    if (!frame) {
        return;
    }

    while (true) {
        int ret = avcodec_receive_frame(audio_codec_ctx_, frame);
        if (ret == 0) {
            DecodedFrame out_frame;
            if (convertAudioFrame(frame, out_frame)) {
                out_frames.push_back(out_frame);
            }
            av_frame_unref(frame);
            continue;
        }
        break;
    }

    av_frame_free(&frame);
}

// ============================================================
// 转换视频帧
// ============================================================
bool MediaDecoder::convertVideoFrame(AVFrame* frame, DecodedFrame& out_frame)
{
    if (!frame || !video_codec_ctx_ || video_stream_index_ == -1) {
        return false;
    }

    int64_t ts = frame->best_effort_timestamp;
    if (ts == AV_NOPTS_VALUE) ts = frame->pts;
    if (ts == AV_NOPTS_VALUE) ts = frame->pkt_dts;

    const double fps = media_info_.video_info.frame_rate > 0.0
        ? media_info_.video_info.frame_rate
        : 25.0;
    double pts = 0.0;
    if (ts != AV_NOPTS_VALUE) {
        pts = ts * av_q2d(format_ctx_->streams[video_stream_index_]->time_base);
        if (!media_info_.has_audio && has_last_video_pts_ && pts <= last_video_pts_) {
            pts = last_video_pts_ + 1.0 / fps;
        }
    }
    else {
        pts = has_last_video_pts_ ? last_video_pts_ + 1.0 / fps : 0.0;
    }
    last_video_pts_ = pts;
    has_last_video_pts_ = true;

    if (frame->format == AV_PIX_FMT_YUV420P) {
        const int y_size = frame->width * frame->height;
        const int chroma_width = (frame->width + 1) / 2;
        const int chroma_height = (frame->height + 1) / 2;
        const int chroma_size = chroma_width * chroma_height;
        const int buf_size = y_size + chroma_size * 2;
        uint8_t* yuv_buf = (uint8_t*)av_malloc(buf_size);
        if (!yuv_buf) {
            return false;
        }

        uint8_t* dst_y = yuv_buf;
        uint8_t* dst_u = dst_y + y_size;
        uint8_t* dst_v = dst_u + chroma_size;
        av_image_copy_plane(dst_y, frame->width, frame->data[0], frame->linesize[0],
            frame->width, frame->height);
        av_image_copy_plane(dst_u, chroma_width, frame->data[1], frame->linesize[1],
            chroma_width, chroma_height);
        av_image_copy_plane(dst_v, chroma_width, frame->data[2], frame->linesize[2],
            chroma_width, chroma_height);

        out_frame.type = DecodedFrame::Type::Video;
        out_frame.video_format = VideoFrameFormat::YUV420P;
        out_frame.data = yuv_buf;
        out_frame.planes[0] = dst_y;
        out_frame.planes[1] = dst_u;
        out_frame.planes[2] = dst_v;
        out_frame.linesizes[0] = frame->width;
        out_frame.linesizes[1] = chroma_width;
        out_frame.linesizes[2] = chroma_width;
        out_frame.video_full_range = (frame->color_range == AVCOL_RANGE_JPEG);
        out_frame.video_bt709 = (frame->width >= 1280 || frame->height >= 720);
        out_frame.data_size = buf_size;
        out_frame.width = frame->width;
        out_frame.height = frame->height;
        out_frame.pts = pts;
        current_time_ = pts;
        return true;
    }

    sws_ctx_ = sws_getCachedContext(
        sws_ctx_,
        frame->width, frame->height, (AVPixelFormat)frame->format,
        frame->width, frame->height, AV_PIX_FMT_RGBA,
        SWS_BILINEAR, nullptr, nullptr, nullptr);
    if (!sws_ctx_) {
        return false;
    }

    int src_full = (frame->color_range == AVCOL_RANGE_JPEG) ? 1 : 0;
    const int* in_cs = (frame->width >= 1280 || frame->height >= 720)
        ? sws_getCoefficients(SWS_CS_ITU709)
        : sws_getCoefficients(SWS_CS_ITU601);
    sws_setColorspaceDetails(sws_ctx_, in_cs, src_full,
        sws_getCoefficients(SWS_CS_ITU601), 1, 0, 1 << 16, 1 << 16);

    int buf_size = frame->width * frame->height * 4;
    uint8_t* rgba_buf = (uint8_t*)av_malloc(buf_size);
    if (!rgba_buf) {
        return false;
    }

    uint8_t* dst[4] = { rgba_buf, nullptr, nullptr, nullptr };
    int dst_stride[4] = { frame->width * 4, 0, 0, 0 };
    sws_scale(sws_ctx_, frame->data, frame->linesize, 0, frame->height, dst, dst_stride);

    out_frame.type = DecodedFrame::Type::Video;
    out_frame.video_format = VideoFrameFormat::RGBA;
    out_frame.data = rgba_buf;
    out_frame.planes[0] = rgba_buf;
    out_frame.linesizes[0] = frame->width * 4;
    out_frame.video_full_range = true;
    out_frame.video_bt709 = false;
    out_frame.data_size = buf_size;
    out_frame.width = frame->width;
    out_frame.height = frame->height;
    out_frame.pts = pts;
    current_time_ = pts;
    return true;
}

// ============================================================
// 转换音频帧
// ============================================================
bool MediaDecoder::convertAudioFrame(AVFrame* frame, DecodedFrame& out_frame)
{
    if (!frame || !swr_ctx_ || !audio_buffer_src_ctx_ || audio_stream_index_ == -1) {
        return false;
    }

    int output_samples = (int)av_rescale_rnd(
        swr_get_delay(swr_ctx_, frame->sample_rate) + frame->nb_samples,
        TARGET_SAMPLE_RATE, frame->sample_rate, AV_ROUND_UP);
    if (output_samples <= 0) {
        return false;
    }

    AVFrame* resampled_frame = av_frame_alloc();
    if (!resampled_frame) {
        return false;
    }

    resampled_frame->format = TARGET_SAMPLE_FMT;
    resampled_frame->sample_rate = TARGET_SAMPLE_RATE;
    av_channel_layout_default(&resampled_frame->ch_layout, TARGET_CHANNELS);
    resampled_frame->nb_samples = output_samples;
    if (av_frame_get_buffer(resampled_frame, 0) < 0) {
        av_channel_layout_uninit(&resampled_frame->ch_layout);
        av_frame_free(&resampled_frame);
        return false;
    }

    int converted_samples = swr_convert(swr_ctx_,
        resampled_frame->data, output_samples,
        (const uint8_t**)frame->data, frame->nb_samples);
    if (converted_samples <= 0) {
        av_channel_layout_uninit(&resampled_frame->ch_layout);
        av_frame_free(&resampled_frame);
        return false;
    }

    resampled_frame->nb_samples = converted_samples;

    if (std::fabs(playback_speed_ - 1.0f) < 0.001f) {
        int data_size = av_samples_get_buffer_size(nullptr, TARGET_CHANNELS,
            converted_samples, TARGET_SAMPLE_FMT, 1);
        if (data_size <= 0) {
            av_channel_layout_uninit(&resampled_frame->ch_layout);
            av_frame_free(&resampled_frame);
            return false;
        }

        uint8_t* output_buffer = (uint8_t*)av_malloc(data_size);
        if (!output_buffer) {
            av_channel_layout_uninit(&resampled_frame->ch_layout);
            av_frame_free(&resampled_frame);
            return false;
        }

        memcpy(output_buffer, resampled_frame->data[0], data_size);

        out_frame.type = DecodedFrame::Type::Audio;
        out_frame.data = output_buffer;
        out_frame.samples = converted_samples;
        out_frame.data_size = data_size;
        out_frame.pts = getFrameTimestampSeconds(frame, audio_stream_index_);
        current_time_ = out_frame.pts;

        av_channel_layout_uninit(&resampled_frame->ch_layout);
        av_frame_free(&resampled_frame);
        return true;
    }

    int64_t src_ts = frame->best_effort_timestamp;
    if (src_ts == AV_NOPTS_VALUE) src_ts = frame->pts;
    if (src_ts == AV_NOPTS_VALUE) src_ts = frame->pkt_dts;
    if (src_ts != AV_NOPTS_VALUE) {
        resampled_frame->pts = av_rescale_q(src_ts,
            format_ctx_->streams[audio_stream_index_]->time_base,
            AVRational{ 1, TARGET_SAMPLE_RATE });
    }
    else {
        resampled_frame->pts = AV_NOPTS_VALUE;
    }

    int ret = av_buffersrc_add_frame_flags(audio_buffer_src_ctx_, resampled_frame, AV_BUFFERSRC_FLAG_KEEP_REF);
    av_channel_layout_uninit(&resampled_frame->ch_layout);
    av_frame_free(&resampled_frame);
    if (ret < 0) {
        return false;
    }

    return pullAudioFilterFrame(out_frame);
}

// ============================================================
// 跳转到指定时间位置
// ============================================================
void MediaDecoder::seek(double seconds)
{
    if (!format_ctx_) {
        return;
    }

    // 音频是播放主时钟，优先按音频流定位能让听感上的 seek 落点更接近目标。
    const int seek_stream_index = (audio_stream_index_ != -1) ? audio_stream_index_ : video_stream_index_;
    if (seek_stream_index == -1) {
        return;
    }

    // 将秒数转换为流的时间戳单位（time_base）
    // FFmpeg内部使用时间戳(pts)而非秒数来表示时间
    // 转换公式：timestamp = seconds / time_base
    AVStream* stream = format_ctx_->streams[seek_stream_index];
    int64_t timestamp = (int64_t)(seconds / av_q2d(stream->time_base));

    // av_seek_frame() 跳转到指定时间戳
    // AVSEEK_FLAG_BACKWARD：向后寻找最近的关键帧
    // （因为非关键帧不能独立解码，必须从关键帧开始）
    av_seek_frame(format_ctx_, seek_stream_index, timestamp, AVSEEK_FLAG_BACKWARD);

    // 刷新解码器缓冲区，清除旧帧
    // 跳转后，解码器中可能还有跳转前的缓存帧，需要清除
    if (video_codec_ctx_) {
        avcodec_flush_buffers(video_codec_ctx_);
    }
    if (audio_codec_ctx_) {
        avcodec_flush_buffers(audio_codec_ctx_);
    }
    if (swr_ctx_) {
        swr_close(swr_ctx_);
        swr_init(swr_ctx_);
    }
    if (media_info_.has_audio) {
        initAudioFilterGraph();
    }

    audio_drained_ = !media_info_.has_audio;
    audio_filter_flush_sent_ = false;
    last_video_pts_ = seconds;
    has_last_video_pts_ = true;

    current_time_ = seconds;
}

double MediaDecoder::getFrameTimestampSeconds(AVFrame* frame, int stream_index) const
{
    if (!frame || !format_ctx_ || stream_index < 0 || stream_index >= (int)format_ctx_->nb_streams) {
        return current_time_.load();
    }

    int64_t ts = frame->best_effort_timestamp;
    if (ts == AV_NOPTS_VALUE) ts = frame->pts;
    if (ts == AV_NOPTS_VALUE) ts = frame->pkt_dts;
    if (ts == AV_NOPTS_VALUE) return current_time_.load();

    return ts * av_q2d(format_ctx_->streams[stream_index]->time_base);
}

// ============================================================
// 提取媒体信息
// ============================================================
void MediaDecoder::extractMediaInfo()
{
    if (!format_ctx_) return;

    // 文件格式信息
    media_info_.format_name = format_ctx_->iformat->name;
    media_info_.duration = (format_ctx_->duration > 0)
        ? static_cast<double>(format_ctx_->duration) / AV_TIME_BASE
        : 0.0;
    media_info_.file_size = format_ctx_->pb ? avio_size(format_ctx_->pb) : 0;
    media_info_.overall_bit_rate = format_ctx_->bit_rate;

    // 视频流信息
    if (video_stream_index_ != -1 && video_codec_ctx_) {
        AVStream* vstream = format_ctx_->streams[video_stream_index_];
        media_info_.video_info.width = video_codec_ctx_->width;
        media_info_.video_info.height = video_codec_ctx_->height;
        // r_frame_rate 对某些容器（如MP4）可能返回异常值（如1000fps）
        // 优先使用 avg_frame_rate，再用 r_frame_rate，最后fallback到25fps
        double fps = av_q2d(vstream->avg_frame_rate);
        if (fps <= 0 || fps > 120) fps = av_q2d(vstream->r_frame_rate);
        if (fps <= 0 || fps > 120) fps = 25.0;
        media_info_.video_info.frame_rate = fps;
        if (media_info_.duration <= 0.0 && vstream->duration > 0) {
            media_info_.duration = vstream->duration * av_q2d(vstream->time_base);
        }
        if (media_info_.duration <= 0.0 && vstream->nb_frames > 0 && fps > 0.0) {
            media_info_.duration = static_cast<double>(vstream->nb_frames) / fps;
        }
        media_info_.video_info.codec_name = avcodec_get_name(video_codec_ctx_->codec_id);
        media_info_.video_info.pixel_format = av_get_pix_fmt_name(video_codec_ctx_->pix_fmt);
        media_info_.video_info.bit_rate = video_codec_ctx_->bit_rate;
        media_info_.video_info.duration = media_info_.duration;
        media_info_.video_info.total_frames = (int)(media_info_.duration * media_info_.video_info.frame_rate);
    }

    // 音频流信息
    if (audio_stream_index_ != -1 && audio_codec_ctx_) {
        media_info_.audio_info.sample_rate = audio_codec_ctx_->sample_rate;
        media_info_.audio_info.channels = audio_codec_ctx_->ch_layout.nb_channels;
        media_info_.audio_info.codec_name = avcodec_get_name(audio_codec_ctx_->codec_id);
        media_info_.audio_info.sample_format = av_get_sample_fmt_name(audio_codec_ctx_->sample_fmt);
        media_info_.audio_info.bit_rate = audio_codec_ctx_->bit_rate;
        media_info_.audio_info.duration = media_info_.duration;
    }
}
