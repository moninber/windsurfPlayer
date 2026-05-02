/**
 * @file VideoWidget.cpp
 * @brief 视频显示控件实现
 */

#include "VideoWidget.h"
#include <iostream>

VideoWidget::VideoWidget(QWidget* parent)
    : QOpenGLWidget(parent)
    , gl_initialized_(false)
    , frame_width_(0)
    , frame_height_(0)
    , has_new_frame_(false)
    , has_new_spectrum_(false)
    , show_spectrum_(false)
{
    // 设置控件属性
    setMinimumSize(640, 360);
    setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Expanding);
    setFocusPolicy(Qt::NoFocus);  // 禁止 OpenGL 窗口拦截焦点，让父窗口 MainWindow 统一处理按键

    // 启动刷新定时器（约60fps）
    // QTimer在Qt事件循环中触发，确保paintGL()在正确的线程调用
    QObject::connect(&refresh_timer_, &QTimer::timeout, this, [this]() {
        if (has_new_frame_ || show_spectrum_) {
            update();  // 请求重绘，Qt会在合适时机调用paintGL()
        }
    });
    refresh_timer_.start(16);  // ~60fps
}

VideoWidget::~VideoWidget()
{
    // 确保OpenGL资源在上下文有效时释放
    makeCurrent();
    renderer_.reset();
    visualizer_.reset();
    doneCurrent();
}

void VideoWidget::initializeGL()
{
    // 初始化Qt的OpenGL函数指针（QOpenGLFunctions方法调用前必须先调用）
    initializeOpenGLFunctions();

    // 再初始化GLEW（VideoRenderer/AudioVisualizer使用GLEW的函数指针）
    glewExperimental = GL_TRUE;
    GLenum err = glewInit();
    if (err != GLEW_OK) {
        std::cerr << "[VideoWidget] GLEW初始化失败: " << glewGetErrorString(err) << std::endl;
        return;
    }
    // glewInit可能设置GL_INVALID_ENUM，清除错误
    glGetError();

    glDisable(GL_DEPTH_TEST);

    // 输出OpenGL信息
    std::cout << "[VideoWidget] OpenGL版本: " << glGetString(GL_VERSION) << std::endl;

    // 创建并初始化渲染器
    renderer_ = std::make_unique<VideoRenderer>();
    visualizer_ = std::make_unique<AudioVisualizer>();

    // 初始大小设为默认值，加载文件后会重新初始化
    renderer_->init(1920, 1080);
    visualizer_->init(64);

    gl_initialized_ = true;
    std::cout << "[VideoWidget] OpenGL初始化完成" << std::endl;
}

void VideoWidget::paintGL()
{
    if (!gl_initialized_) return;

    if (show_spectrum_) {
        // 频谱可视化模式
        glClearColor(0.05f, 0.05f, 0.08f, 1.0f);
        glClear(GL_COLOR_BUFFER_BIT);

        std::lock_guard<std::mutex> lock(spectrum_mutex_);
        if (visualizer_ && (!spectrum_data_.empty() || has_new_spectrum_)) {
            visualizer_->render(width(), height());
            has_new_spectrum_ = false;
        }
    }
    else {
        // 视频渲染模式
        glClearColor(0.02f, 0.02f, 0.04f, 1.0f);
        glClear(GL_COLOR_BUFFER_BIT);

        std::lock_guard<std::mutex> lock(frame_mutex_);
        if (!frame_data_.empty()) {
            renderer_->setViewport(0, 0, width(), height());
            renderer_->render(frame_data_.data(), frame_width_, frame_height_);
            has_new_frame_ = false;
        }
    }
}

void VideoWidget::resizeGL(int w, int h)
{
    if (renderer_) {
        renderer_->resize(w, h);
    }
}

void VideoWidget::setVideoFrame(const uint8_t* data, int width, int height)
{
    if (!data) return;

    std::lock_guard<std::mutex> lock(frame_mutex_);
    size_t data_size = width * height * 4;  // RGBA
    frame_data_.resize(data_size);
    memcpy(frame_data_.data(), data, data_size);
    frame_width_ = width;
    frame_height_ = height;
    has_new_frame_ = true;
}

void VideoWidget::setSpectrumData(const std::vector<float>& spectrum)
{
    std::lock_guard<std::mutex> lock(spectrum_mutex_);
    spectrum_data_ = spectrum;
    if (visualizer_) {
        visualizer_->setSpectrumData(spectrum_data_);
    }
    has_new_spectrum_ = true;
}

void VideoWidget::setShowSpectrum(bool show)
{
    show_spectrum_ = show;
    if (!show) {
        std::lock_guard<std::mutex> lock(spectrum_mutex_);
        has_new_spectrum_ = false;
    }
    update();
}

void VideoWidget::setEffect(VideoEffect effect)
{
    if (renderer_) {
        renderer_->setEffect(effect);
    }
}

void VideoWidget::clearFrame()
{
    std::lock_guard<std::mutex> lock(frame_mutex_);
    frame_data_.clear();
    frame_width_ = 0;
    frame_height_ = 0;
    has_new_frame_ = false;
    update();
}
