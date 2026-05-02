/**
 * @file VideoWidget.h
 * @brief 视频显示控件 - 基于QOpenGLWidget的视频渲染与频谱可视化
 * 
 * QOpenGLWidget是Qt提供的OpenGL渲染控件：
 * - 自动管理OpenGL上下文的生命周期
 * - 与Qt事件循环无缝集成
 * - 支持在Qt布局系统中自由放置
 * 
 * 渲染流程：
 * 1. initializeGL() → 创建着色器、纹理、几何体（仅调用一次）
 * 2. resizeGL()     → 调整视口大小
 * 3. paintGL()      → 每帧渲染（由update()触发）
 * 
 * 帧数据传递：
 * - PlayerController在解码线程中存储RGB帧数据到共享缓冲区
 * - VideoWidget通过QTimer定期调用update()触发重绘
 * - paintGL()中从缓冲区读取最新帧并渲染
 * 
 * 这种设计确保：
 * - OpenGL调用只在Qt线程（拥有OpenGL上下文的线程）中执行
 * - 解码线程不接触任何OpenGL对象
 * - 线程安全通过mutex保护共享缓冲区
 */

#pragma once

// GLEW必须在所有OpenGL头文件之前包含（包括Qt的OpenGL头）
// 否则会报错: "gl.h included before glew.h"
#include <GL/glew.h>

#include <QOpenGLWidget>
#include <QOpenGLFunctions>
#include <QTimer>
#include <mutex>
#include <memory>
#include <vector>
#include "VideoRenderer.h"
#include "AudioVisualizer.h"

class VideoWidget : public QOpenGLWidget, protected QOpenGLFunctions {
    Q_OBJECT

public:
    explicit VideoWidget(QWidget* parent = nullptr);
    ~VideoWidget();

    /**
     * @brief 设置视频帧数据（由PlayerController调用，线程安全）
     * @param data RGB24像素数据
     * @param width 帧宽度
     * @param height 帧高度
     */
    void setVideoFrame(const uint8_t* data, int width, int height);

    /**
     * @brief 设置频谱数据（由PlayerController调用，线程安全）
     * @param spectrum 频谱数据（归一化振幅0~1）
     */
    void setSpectrumData(const std::vector<float>& spectrum);

    /** @brief 切换视频/频谱显示模式 */
    void setShowSpectrum(bool show);

    /** @brief 设置视频特效 */
    void setEffect(VideoEffect effect);

    /** @brief 清除当前显示（停止播放时调用） */
    void clearFrame();

protected:
    // QOpenGLWidget虚函数
    void initializeGL() override;
    void paintGL() override;
    void resizeGL(int w, int h) override;

private:
    // OpenGL渲染器
    std::unique_ptr<VideoRenderer> renderer_;
    std::unique_ptr<AudioVisualizer> visualizer_;
    bool gl_initialized_;

    // 帧数据缓冲区（线程安全）
    std::mutex frame_mutex_;
    std::vector<uint8_t> frame_data_;
    int frame_width_;
    int frame_height_;
    bool has_new_frame_;

    // 频谱数据缓冲区
    std::mutex spectrum_mutex_;
    std::vector<float> spectrum_data_;
    bool has_new_spectrum_;

    // 显示模式
    bool show_spectrum_;

    // 刷新定时器
    QTimer refresh_timer_;
};
