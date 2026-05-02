/**
 * @file MediaInfo.h
 * @brief 媒体信息数据结构定义
 * 
 * 本文件定义了媒体文件的各种元数据结构，包括：
 * - 视频流信息（分辨率、帧率、编码格式等）
 * - 音频流信息（采样率、声道数、编码格式等）
 * - 媒体文件整体信息（时长、格式、文件路径等）
 * 
 * 这些结构体用于在解码器、渲染器、数据库之间传递媒体信息，
 * 也是MySQL数据库存储的核心数据模型。
 */

#pragma once

#include <string>
#include <vector>

// ============================================================
// 视频流信息结构体
// ============================================================
struct VideoStreamInfo {
    int width = 0;              // 视频宽度（像素）
    int height = 0;             // 视频高度（像素）
    double frame_rate = 0.0;    // 帧率（fps）
    std::string codec_name;     // 编码器名称（如 h264, hevc, vp9）
    std::string pixel_format;   // 像素格式（如 yuv420p, rgb24）
    int bit_rate = 0;           // 视频码率（bps）
    double duration = 0.0;      // 视频时长（秒）
    int total_frames = 0;       // 总帧数
};

// ============================================================
// 音频流信息结构体
// ============================================================
struct AudioStreamInfo {
    int sample_rate = 0;        // 采样率（Hz，如 44100, 48000）
    int channels = 0;           // 声道数（1=单声道, 2=立体声）
    std::string codec_name;     // 编码器名称（如 aac, mp3, opus）
    std::string sample_format;  // 采样格式（如 s16, flt）
    int bit_rate = 0;           // 音频码率（bps）
    double duration = 0.0;      // 音频时长（秒）
};

// ============================================================
// 媒体文件完整信息结构体
// ============================================================
struct MediaInfo {
    std::string file_path;              // 文件完整路径
    std::string file_name;              // 文件名（不含路径）
    std::string format_name;            // 容器格式（如 mp4, mkv, avi）
    double duration = 0.0;              // 总时长（秒）
    long long file_size = 0;            // 文件大小（字节）
    int overall_bit_rate = 0;           // 总码率（bps）

    VideoStreamInfo video_info;         // 视频流信息
    AudioStreamInfo audio_info;         // 音频流信息
    bool has_video = false;             // 是否包含视频流
    bool has_audio = false;             // 是否包含音频流

    // 数据库相关字段
    int db_id = -1;                     // 数据库记录ID（-1表示未入库）
    std::string title;                  // 媒体标题（可自定义）
    std::string tags;                   // 标签（逗号分隔）
    bool is_favorite = false;           // 是否收藏
    std::string added_time;             // 添加到库的时间
    int play_count = 0;                 // 播放次数
    std::string last_play_time;         // 最后播放时间
};

// ============================================================
// 视频效果枚举
// ============================================================
enum class VideoEffect {
    None = 0,           // 无效果（原始渲染）
    Grayscale,          // 灰度效果
    Invert,             // 反色效果
    EdgeDetect,         // 边缘检测（Sobel算子）
    Blur,               // 模糊效果（盒式模糊）
    BrightnessContrast, // 亮度/对比度调节
    Sepia,              // 怀旧色调效果
    Count               // 效果总数（用于循环遍历）
};

// ============================================================
// 应用模式枚举
// ============================================================
enum class AppMode {
    Player,             // 播放器模式（视频播放+效果）
    Visualizer,         // 可视化模式（音频频谱）
    Library,            // 媒体库模式（浏览数据库）
    Transcoder          // 转码模式（格式转换）
};

// ============================================================
// 辅助函数：将VideoEffect转为可读字符串
// ============================================================
inline const char* videoEffectToString(VideoEffect effect) {
    switch (effect) {
        case VideoEffect::None:              return "原始";
        case VideoEffect::Grayscale:         return "灰度";
        case VideoEffect::Invert:            return "反色";
        case VideoEffect::EdgeDetect:        return "边缘检测";
        case VideoEffect::Blur:              return "模糊";
        case VideoEffect::BrightnessContrast: return "亮度对比度";
        case VideoEffect::Sepia:             return "怀旧色调";
        default:                             return "未知";
    }
}
