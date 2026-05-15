/**
 * @file MediaTranscoder.cpp
 * @brief 媒体转码器实现
 * 
 * 注意：转码功能需要FFmpeg编码器支持
 * - libx264：H.264编码器（最广泛支持）
 * - libx265：H.265/HEVC编码器（更高压缩率）
 * - aac：AAC音频编码器
 * - libmp3lame：MP3编码器
 * 
 * 确保FFmpeg编译时启用了这些编码器（--enable-libx264等）
 */

#include "MediaTranscoder.h"
#include <iostream>

MediaTranscoder::MediaTranscoder()
    : cancelled_(false)
{
}

MediaTranscoder::~MediaTranscoder()
{
}

void MediaTranscoder::cancel()
{
    cancelled_ = true;
}

// ============================================================
// 主转码函数
// ============================================================
bool MediaTranscoder::transcode(
    const std::string& input_path,
    const std::string& output_path,
    int target_width,
    int target_height,
    const std::string& video_codec,
    const std::string& audio_codec,
    int bitrate,
    std::function<void(float)> progress_cb)
{
    cancelled_ = false;
    last_error_.clear();

    // --------------------------------------------------------
    // 步骤1：打开输入文件
    // --------------------------------------------------------
    AVFormatContext* in_fmt_ctx = nullptr;
    if (avformat_open_input(&in_fmt_ctx, input_path.c_str(), nullptr, nullptr) != 0) {
        last_error_ = "无法打开输入文件: " + input_path;
        std::cerr << "[Transcoder] " << last_error_ << std::endl;
        return false;
    }

    if (avformat_find_stream_info(in_fmt_ctx, nullptr) < 0) {
        last_error_ = "无法获取输入流信息";
        avformat_close_input(&in_fmt_ctx);
        return false;
    }

    // --------------------------------------------------------
    // 步骤2：创建输出格式上下文
    // avformat_alloc_output_context2() 根据输出文件扩展名自动选择封装格式
    // 例如：.mp4 → MOV格式, .mkv → Matroska格式
    // --------------------------------------------------------
    AVFormatContext* out_fmt_ctx = nullptr;
    if (avformat_alloc_output_context2(&out_fmt_ctx, nullptr, nullptr, output_path.c_str()) < 0) {
        last_error_ = "无法创建输出格式上下文";
        avformat_close_input(&in_fmt_ctx);
        return false;
    }

    // --------------------------------------------------------
    // 步骤3：为每个输入流创建对应的输出流
    // --------------------------------------------------------
    int video_in_idx = -1, audio_in_idx = -1;
    int video_out_idx = -1, audio_out_idx = -1;
    AVCodecContext* video_dec_ctx = nullptr;
    AVCodecContext* audio_dec_ctx = nullptr;
    AVCodecContext* video_enc_ctx = nullptr;
    AVCodecContext* audio_enc_ctx = nullptr;
    SwsContext* sws_ctx = nullptr;
    SwrContext* swr_ctx = nullptr;

    for (unsigned int i = 0; i < in_fmt_ctx->nb_streams; i++) {
        AVStream* in_stream = in_fmt_ctx->streams[i];
        AVCodecParameters* in_codecpar = in_stream->codecpar;

        if (in_codecpar->codec_type == AVMEDIA_TYPE_VIDEO && video_in_idx == -1) {
            video_in_idx = i;

            // 打开视频解码器
            const AVCodec* dec_codec = avcodec_find_decoder(in_codecpar->codec_id);
            if (!dec_codec) {
                last_error_ = "找不到视频解码器: " + std::string(avcodec_get_name(in_codecpar->codec_id));
                goto cleanup;
            }

            video_dec_ctx = avcodec_alloc_context3(dec_codec);
            if (!video_dec_ctx) {
                last_error_ = "无法分配视频解码器上下文";
                goto cleanup;
            }

            if (avcodec_parameters_to_context(video_dec_ctx, in_codecpar) < 0) {
                last_error_ = "无法复制视频解码参数";
                goto cleanup;
            }

            if (avcodec_open2(video_dec_ctx, dec_codec, nullptr) < 0) {
                last_error_ = "无法打开视频解码器: " + std::string(dec_codec->name);
                goto cleanup;
            }

            // 创建视频编码器
            const AVCodec* enc_codec = avcodec_find_encoder_by_name(video_codec.c_str());
            if (!enc_codec) {
                // 回退到自动查找
                enc_codec = avcodec_find_encoder(in_codecpar->codec_id);
            }
            if (!enc_codec) {
                last_error_ = "找不到视频编码器: " + video_codec;
                goto cleanup;
            }

            video_enc_ctx = avcodec_alloc_context3(enc_codec);
            if (!video_enc_ctx) {
                last_error_ = "无法分配视频编码器上下文";
                goto cleanup;
            }

            // 设置编码参数
            int out_width = target_width > 0 ? target_width : video_dec_ctx->width;
            int out_height = target_height > 0 ? target_height : video_dec_ctx->height;
            // 确保宽高是2的倍数（H.264/H.265要求）
            out_width = out_width & ~1;
            out_height = out_height & ~1;

            video_enc_ctx->width = out_width;
            video_enc_ctx->height = out_height;

            // 修正 time_base 和 framerate 的获取
            AVRational fps = av_guess_frame_rate(in_fmt_ctx, in_stream, nullptr);
            if (fps.num <= 0 || fps.den <= 0) fps = AVRational{ 25, 1 };
            video_enc_ctx->time_base = av_inv_q(fps);
            video_enc_ctx->framerate = fps;

            // 强制使用 YUV420P 以获得最佳 MP4 兼容性
            video_enc_ctx->pix_fmt = AV_PIX_FMT_YUV420P;
            // 检查编码器是否真的支持 YUV420P
#pragma warning(disable: 4996)
            if (enc_codec->pix_fmts) {
                bool supported = false;
                for (const enum AVPixelFormat* p = enc_codec->pix_fmts; *p != AV_PIX_FMT_NONE; p++) {
                    if (*p == AV_PIX_FMT_YUV420P) {
                        supported = true;
                        break;
                    }
                }
                if (!supported) video_enc_ctx->pix_fmt = enc_codec->pix_fmts[0];
            }
#pragma warning(default: 4996)
            video_enc_ctx->bit_rate = bitrate > 0 ? bitrate : video_dec_ctx->bit_rate;

            // H.264/H.265编码预设
            if (video_codec.find("264") != std::string::npos ||
                video_codec.find("265") != std::string::npos ||
                video_codec.find("hevc") != std::string::npos) {
                av_opt_set(video_enc_ctx->priv_data, "preset", "medium", 0);
                av_opt_set(video_enc_ctx->priv_data, "crf", "23", 0);
            }

            if (out_fmt_ctx->oformat->flags & AVFMT_GLOBALHEADER) {
                video_enc_ctx->flags |= AV_CODEC_FLAG_GLOBAL_HEADER;
            }

            if (avcodec_open2(video_enc_ctx, enc_codec, nullptr) < 0) {
                last_error_ = "无法打开视频编码器";
                goto cleanup;
            }

            // 创建输出流
            AVStream* out_stream = avformat_new_stream(out_fmt_ctx, enc_codec);
            if (!out_stream) {
                last_error_ = "无法创建视频输出流";
                goto cleanup;
            }
            if (avcodec_parameters_from_context(out_stream->codecpar, video_enc_ctx) < 0) {
                last_error_ = "无法复制视频编码参数到输出流";
                goto cleanup;
            }
            out_stream->time_base = video_enc_ctx->time_base;
            video_out_idx = out_stream->index;

            // sws_ctx 延迟到 processVideoStream 中根据实际帧格式初始化
            sws_ctx = nullptr;

            std::cout << "[Transcoder] 视频转码: "
                      << video_dec_ctx->width << "x" << video_dec_ctx->height
                      << " → " << out_width << "x" << out_height
                      << ", 编码: " << video_codec << std::endl;
        }
        else if (in_codecpar->codec_type == AVMEDIA_TYPE_AUDIO && audio_in_idx == -1) {
            audio_in_idx = i;

            // 打开音频解码器
            const AVCodec* dec_codec = avcodec_find_decoder(in_codecpar->codec_id);
            if (!dec_codec) {
                last_error_ = "找不到音频解码器: " + std::string(avcodec_get_name(in_codecpar->codec_id));
                goto cleanup;
            }

            audio_dec_ctx = avcodec_alloc_context3(dec_codec);
            if (!audio_dec_ctx) {
                last_error_ = "无法分配音频解码器上下文";
                goto cleanup;
            }

            if (avcodec_parameters_to_context(audio_dec_ctx, in_codecpar) < 0) {
                last_error_ = "无法复制音频解码参数";
                goto cleanup;
            }

            if (avcodec_open2(audio_dec_ctx, dec_codec, nullptr) < 0) {
                last_error_ = "无法打开音频解码器: " + std::string(dec_codec->name);
                goto cleanup;
            }

            // 创建音频编码器
            const AVCodec* enc_codec = avcodec_find_encoder_by_name(audio_codec.c_str());
            if (!enc_codec) {
                enc_codec = avcodec_find_encoder(AV_CODEC_ID_AAC);
            }
            if (!enc_codec) {
                last_error_ = "找不到音频编码器: " + audio_codec;
                goto cleanup;
            }

            audio_enc_ctx = avcodec_alloc_context3(enc_codec);
            if (!audio_enc_ctx) {
                last_error_ = "无法分配音频编码器上下文";
                goto cleanup;
            }

            audio_enc_ctx->sample_rate = audio_dec_ctx->sample_rate;
            audio_enc_ctx->bit_rate = audio_dec_ctx->bit_rate > 0 ? audio_dec_ctx->bit_rate : 128000;

            // 设置声道布局
            av_channel_layout_default(&audio_enc_ctx->ch_layout, audio_dec_ctx->ch_layout.nb_channels);

            // AVCodec::sample_fmts已废弃但仍可用，抑制废弃警告
#pragma warning(disable: 4996)
            audio_enc_ctx->sample_fmt = enc_codec->sample_fmts ? enc_codec->sample_fmts[0] : AV_SAMPLE_FMT_FLTP;
#pragma warning(default: 4996)

            if (audio_enc_ctx->sample_rate == 0) {
                audio_enc_ctx->sample_rate = 44100;
            }
            audio_enc_ctx->time_base = AVRational{1, audio_enc_ctx->sample_rate};

            if (out_fmt_ctx->oformat->flags & AVFMT_GLOBALHEADER) {
                audio_enc_ctx->flags |= AV_CODEC_FLAG_GLOBAL_HEADER;
            }

            if (avcodec_open2(audio_enc_ctx, enc_codec, nullptr) < 0) {
                last_error_ = "无法打开音频编码器";
                goto cleanup;
            }

            AVStream* out_stream = avformat_new_stream(out_fmt_ctx, enc_codec);
            if (!out_stream) {
                last_error_ = "无法创建音频输出流";
                goto cleanup;
            }
            if (avcodec_parameters_from_context(out_stream->codecpar, audio_enc_ctx) < 0) {
                last_error_ = "无法复制音频编码参数到输出流";
                goto cleanup;
            }
            out_stream->time_base = audio_enc_ctx->time_base;
            audio_out_idx = out_stream->index;

            // 创建重采样上下文
            swr_alloc_set_opts2(&swr_ctx,
                &audio_enc_ctx->ch_layout, audio_enc_ctx->sample_fmt, audio_enc_ctx->sample_rate,
                &audio_dec_ctx->ch_layout, audio_dec_ctx->sample_fmt, audio_dec_ctx->sample_rate,
                0, nullptr);
            if (swr_ctx) swr_init(swr_ctx);

            std::cout << "[Transcoder] 音频转码: 编码=" << audio_codec << std::endl;
        }
    }

    // --------------------------------------------------------
    // 步骤4：打开输出文件并写入头部
    // --------------------------------------------------------
    if (!(out_fmt_ctx->oformat->flags & AVFMT_NOFILE)) {
        if (avio_open(&out_fmt_ctx->pb, output_path.c_str(), AVIO_FLAG_WRITE) < 0) {
            last_error_ = "无法打开输出文件: " + output_path;
            goto cleanup;
        }
    }

    if (avformat_write_header(out_fmt_ctx, nullptr) < 0) {
        last_error_ = "写入文件头失败";
        goto cleanup;
    }

    // --------------------------------------------------------
    // 步骤5：转码循环
    // 读取 → 解码 → 转换 → 编码 → 写入
    // --------------------------------------------------------
    {
        AVPacket* packet = av_packet_alloc();
        AVFrame* frame = av_frame_alloc();
        int64_t total_duration = in_fmt_ctx->duration;

        while (!cancelled_ && av_read_frame(in_fmt_ctx, packet) >= 0) {
            if (packet->stream_index == video_in_idx && video_dec_ctx && video_enc_ctx) {
                processVideoStream(out_fmt_ctx, video_dec_ctx, video_enc_ctx,
                    in_fmt_ctx->streams[video_in_idx]->time_base, video_out_idx, packet, frame, &sws_ctx,
                    total_duration, progress_cb);
            }
            else if (packet->stream_index == audio_in_idx && audio_dec_ctx && audio_enc_ctx) {
                processAudioStream(out_fmt_ctx, audio_dec_ctx, audio_enc_ctx,
                    in_fmt_ctx->streams[audio_in_idx]->time_base, audio_out_idx, packet, frame, &swr_ctx,
                    total_duration, progress_cb);
            }
            av_packet_unref(packet);
        }

        // 刷新解码器并处理剩余帧
        if (video_dec_ctx && video_enc_ctx) {
            avcodec_send_packet(video_dec_ctx, nullptr);
            processVideoStream(out_fmt_ctx, video_dec_ctx, video_enc_ctx,
                in_fmt_ctx->streams[video_in_idx]->time_base, video_out_idx, nullptr, frame, &sws_ctx,
                total_duration, progress_cb);
        }
        if (audio_dec_ctx && audio_enc_ctx) {
            avcodec_send_packet(audio_dec_ctx, nullptr);
            processAudioStream(out_fmt_ctx, audio_dec_ctx, audio_enc_ctx,
                in_fmt_ctx->streams[audio_in_idx]->time_base, audio_out_idx, nullptr, frame, &swr_ctx,
                total_duration, progress_cb);
        }

        // 刷新编码器缓冲区
        if (video_enc_ctx) {
            avcodec_send_frame(video_enc_ctx, nullptr);
            AVPacket* enc_pkt = av_packet_alloc();
            while (avcodec_receive_packet(video_enc_ctx, enc_pkt) == 0) {
                av_packet_rescale_ts(enc_pkt, video_enc_ctx->time_base,
                    out_fmt_ctx->streams[video_out_idx]->time_base);
                enc_pkt->stream_index = video_out_idx;
                av_interleaved_write_frame(out_fmt_ctx, enc_pkt);
                av_packet_unref(enc_pkt);
            }
            av_packet_free(&enc_pkt);
        }
        if (audio_enc_ctx) {
            avcodec_send_frame(audio_enc_ctx, nullptr);
            AVPacket* enc_pkt = av_packet_alloc();
            while (avcodec_receive_packet(audio_enc_ctx, enc_pkt) == 0) {
                av_packet_rescale_ts(enc_pkt, audio_enc_ctx->time_base,
                    out_fmt_ctx->streams[audio_out_idx]->time_base);
                enc_pkt->stream_index = audio_out_idx;
                av_interleaved_write_frame(out_fmt_ctx, enc_pkt);
                av_packet_unref(enc_pkt);
            }
            av_packet_free(&enc_pkt);
        }

        av_frame_free(&frame);
        av_packet_free(&packet);

        // 写入文件尾部
        av_write_trailer(out_fmt_ctx);
    }

    std::cout << "[Transcoder] 转码完成: " << output_path << std::endl;

cleanup:
    // 释放资源
    if (sws_ctx) sws_freeContext(sws_ctx);
    if (swr_ctx) swr_free(&swr_ctx);
    if (video_dec_ctx) avcodec_free_context(&video_dec_ctx);
    if (audio_dec_ctx) avcodec_free_context(&audio_dec_ctx);
    if (video_enc_ctx) avcodec_free_context(&video_enc_ctx);
    if (audio_enc_ctx) avcodec_free_context(&audio_enc_ctx);
    if (out_fmt_ctx) {
        if (!(out_fmt_ctx->oformat->flags & AVFMT_NOFILE) && out_fmt_ctx->pb) {
            avio_closep(&out_fmt_ctx->pb);
        }
        avformat_free_context(out_fmt_ctx);
    }
    if (in_fmt_ctx) avformat_close_input(&in_fmt_ctx);

    return last_error_.empty();
}

// ============================================================
// 处理视频流
// ============================================================
bool MediaTranscoder::processVideoStream(
    AVFormatContext* out_fmt_ctx,
    AVCodecContext* dec_ctx,
    AVCodecContext* enc_ctx,
    AVRational in_time_base,
    int out_stream_idx,
    AVPacket* packet,
    AVFrame* frame,
    SwsContext** sws_ctx,
    int64_t total_duration,
    std::function<void(float)>& progress_cb)
{
    int ret = avcodec_send_packet(dec_ctx, packet);
    if (ret < 0) return true; // 继续处理下一个包

    while (avcodec_receive_frame(dec_ctx, frame) == 0) {
        // 报告进度
        if (progress_cb && total_duration > 0) {
            float progress = std::min(1.0f, (float)(frame->pts * av_q2d(in_time_base) / (total_duration / AV_TIME_BASE)));
            progress_cb(progress);
        }

        // 延迟初始化或检查 sws_ctx 状态
        if (!*sws_ctx) {
            *sws_ctx = sws_getContext(
                frame->width, frame->height, (AVPixelFormat)frame->format,
                enc_ctx->width, enc_ctx->height, enc_ctx->pix_fmt,
                SWS_BILINEAR, nullptr, nullptr, nullptr);
        }

        // 缩放视频帧
        AVFrame* out_frame = av_frame_alloc();
        if (!out_frame) continue;
        
        out_frame->format = enc_ctx->pix_fmt;
        out_frame->width = enc_ctx->width;
        out_frame->height = enc_ctx->height;
        
        if (av_frame_get_buffer(out_frame, 0) < 0) {
            av_frame_free(&out_frame);
            continue;
        }

        if (*sws_ctx) {
            sws_scale(*sws_ctx,
                frame->data, frame->linesize, 0, frame->height,
                out_frame->data, out_frame->linesize);
        } else {
            // 如果仍然没有上下文，说明格式转换有问题
            av_frame_free(&out_frame);
            continue;
        }

        // 处理 PTS
        int64_t pts = frame->pts;
        if (pts == AV_NOPTS_VALUE) pts = frame->pkt_dts;
        if (pts == AV_NOPTS_VALUE) pts = 0; // 兜底处理

        out_frame->pts = av_rescale_q(pts, in_time_base, enc_ctx->time_base);

        // 编码
        ret = avcodec_send_frame(enc_ctx, out_frame);
        av_frame_free(&out_frame);

        if (ret >= 0) {
            AVPacket* enc_pkt = av_packet_alloc();
            while (avcodec_receive_packet(enc_ctx, enc_pkt) == 0) {
                av_packet_rescale_ts(enc_pkt, enc_ctx->time_base,
                    out_fmt_ctx->streams[out_stream_idx]->time_base);
                enc_pkt->stream_index = out_stream_idx;
                av_interleaved_write_frame(out_fmt_ctx, enc_pkt);
                av_packet_unref(enc_pkt);
            }
            av_packet_free(&enc_pkt);
        }

        av_frame_unref(frame);
    }
    return true;
}

// ============================================================
// 处理音频流
// ============================================================
bool MediaTranscoder::processAudioStream(
    AVFormatContext* out_fmt_ctx,
    AVCodecContext* dec_ctx,
    AVCodecContext* enc_ctx,
    AVRational in_time_base,
    int out_stream_idx,
    AVPacket* packet,
    AVFrame* frame,
    SwrContext** swr_ctx,
    int64_t total_duration,
    std::function<void(float)>& progress_cb)
{
    int ret = avcodec_send_packet(dec_ctx, packet);
    if (ret < 0) return true;

    while (avcodec_receive_frame(dec_ctx, frame) == 0) {
        if (*swr_ctx || (frame->format != enc_ctx->sample_fmt || frame->sample_rate != enc_ctx->sample_rate)) {
            // 延迟初始化或检查 swr_ctx 状态
            if (!*swr_ctx) {
                swr_alloc_set_opts2(swr_ctx,
                    &enc_ctx->ch_layout, enc_ctx->sample_fmt, enc_ctx->sample_rate,
                    &frame->ch_layout, (AVSampleFormat)frame->format, frame->sample_rate,
                    0, nullptr);
                if (*swr_ctx) swr_init(*swr_ctx);
            }

            // 1. 估算输出样本数
            int out_samples = (int)av_rescale_rnd(
                swr_get_delay(*swr_ctx, frame->sample_rate) + frame->nb_samples,
                enc_ctx->sample_rate, frame->sample_rate, AV_ROUND_UP);

            // 2. 创建输出帧并分配缓冲区
            AVFrame* out_frame = av_frame_alloc();
            out_frame->format = enc_ctx->sample_fmt;
            av_channel_layout_copy(&out_frame->ch_layout, &enc_ctx->ch_layout);
            out_frame->sample_rate = enc_ctx->sample_rate;
            out_frame->nb_samples = out_samples;
            
            if (av_frame_get_buffer(out_frame, 0) < 0) {
                av_frame_free(&out_frame);
                av_frame_unref(frame);
                continue;
            }

            // 3. 重采样直接到输出帧缓冲区
            int converted = swr_convert(*swr_ctx, out_frame->data, out_samples,
                (const uint8_t**)frame->data, frame->nb_samples);

            if (converted > 0) {
                out_frame->nb_samples = converted;
                
                int64_t pts = frame->pts;
                if (pts == AV_NOPTS_VALUE) pts = frame->pkt_dts;
                if (pts == AV_NOPTS_VALUE) pts = 0;

                out_frame->pts = av_rescale_q(pts, in_time_base, enc_ctx->time_base);

                ret = avcodec_send_frame(enc_ctx, out_frame);
                if (ret >= 0) {
                    AVPacket* enc_pkt = av_packet_alloc();
                    while (avcodec_receive_packet(enc_ctx, enc_pkt) == 0) {
                        av_packet_rescale_ts(enc_pkt, enc_ctx->time_base,
                            out_fmt_ctx->streams[out_stream_idx]->time_base);
                        enc_pkt->stream_index = out_stream_idx;
                        av_interleaved_write_frame(out_fmt_ctx, enc_pkt);
                        av_packet_unref(enc_pkt);
                    }
                    av_packet_free(&enc_pkt);
                }
            }
            av_frame_free(&out_frame);
        }

        av_frame_unref(frame);
    }
    return true;
}
