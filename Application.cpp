/**
 * @file Application.cpp
 * @brief 应用程序主类实现
 */

#include "Application.h"
#include <iostream>
#include <thread>
#include <chrono>

// ============================================================
// UI着色器源码
// ============================================================
static const char* ui_vertex_shader = R"(
#version 330 core
layout (location = 0) in vec2 aPos;
void main() {
    gl_Position = vec4(aPos, 0.0, 1.0);
}
)";

static const char* ui_fragment_shader = R"(
#version 330 core
uniform vec3 uColor;
out vec4 FragColor;
void main() {
    FragColor = vec4(uColor, 1.0);
}
)";

// ============================================================
// 构造/析构
// ============================================================
Application::Application()
    : window_(nullptr)
    , window_width_(1280)
    , window_height_(720)
    , ui_shader_program_(0)
    , ui_vao_(0)
    , ui_vbo_(0)
    , ui_ebo_(0)
    , current_mode_(AppMode::Player)
    , show_playlist_(false)
    , show_info_(true)
    , db_connected_(false)
    , console_input_enabled_(true)
{
}

Application::~Application()
{
    cleanup();
}

// ============================================================
// 初始化
// ============================================================
bool Application::init(int width, int height)
{
    window_width_ = width;
    window_height_ = height;

    // --------------------------------------------------------
    // 步骤1：初始化GLFW
    // glfwInit() 初始化GLFW库，必须在任何GLFW调用之前
    // --------------------------------------------------------
    if (!glfwInit()) {
        std::cerr << "[App] GLFW初始化失败" << std::endl;
        return false;
    }

    // 设置OpenGL版本和配置
    // OpenGL 3.3 Core Profile：现代OpenGL，不包含固定管线函数
    glfwWindowHint(GLFW_CONTEXT_VERSION_MAJOR, 3);
    glfwWindowHint(GLFW_CONTEXT_VERSION_MINOR, 3);
    glfwWindowHint(GLFW_OPENGL_PROFILE, GLFW_OPENGL_CORE_PROFILE);

    // 创建窗口
    window_ = glfwCreateWindow(window_width_, window_height_,
        "MediaStudio - 音视频工作站", nullptr, nullptr);
    if (!window_) {
        std::cerr << "[App] 窗口创建失败" << std::endl;
        glfwTerminate();
        return false;
    }

    // 设置当前OpenGL上下文
    glfwMakeContextCurrent(window_);

    // 注册回调函数
    glfwSetWindowUserPointer(window_, this);
    glfwSetKeyCallback(window_, keyCallback);
    glfwSetMouseButtonCallback(window_, mouseButtonCallback);
    glfwSetScrollCallback(window_, scrollCallback);
    glfwSetFramebufferSizeCallback(window_, framebufferSizeCallback);
    glfwSetWindowCloseCallback(window_, windowCloseCallback);

    // --------------------------------------------------------
    // 步骤2：初始化GLEW
    // GLEW管理OpenGL扩展函数指针
    // 必须在创建OpenGL上下文之后调用
    // --------------------------------------------------------
    if (glewInit() != GLEW_OK) {
        std::cerr << "[App] GLEW初始化失败" << std::endl;
        return false;
    }

    // 输出OpenGL信息
    std::cout << "[App] OpenGL版本: " << glGetString(GL_VERSION) << std::endl;
    std::cout << "[App] 渲染器: " << glGetString(GL_RENDERER) << std::endl;

    // --------------------------------------------------------
    // 步骤3：初始化核心组件
    // --------------------------------------------------------
    renderer_ = std::make_unique<VideoRenderer>();
    visualizer_ = std::make_unique<AudioVisualizer>();
    database_ = std::make_unique<MediaDatabase>();
    transcoder_ = std::make_unique<MediaTranscoder>();

    player_ = std::make_unique<PlayerController>();
    player_->setRenderer(renderer_.get());
    player_->setVisualizer(visualizer_.get());
    player_->setDatabase(database_.get());

    // 初始化可视化器
    visualizer_->init(64);

    // 初始化UI着色器
    if (!initUIShader()) {
        std::cerr << "[App] UI着色器初始化失败" << std::endl;
        return false;
    }
    initUIGeometry();

    // 启用混合（用于半透明UI）
    glEnable(GL_BLEND);
    glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);

    std::cout << "[App] 初始化成功" << std::endl;
    return true;
}

// ============================================================
// 主循环
// ============================================================
void Application::run()
{
    // 打印控制说明
    std::cout << "\n========== MediaStudio 控制说明 ==========" << std::endl;
    std::cout << "  SPACE       - 播放/暂停" << std::endl;
    std::cout << "  LEFT/RIGHT  - 后退5秒/前进5秒" << std::endl;
    std::cout << "  UP/DOWN     - 音量增大/减小" << std::endl;
    std::cout << "  N/P         - 下一曲/上一曲" << std::endl;
    std::cout << "  E           - 切换视频特效" << std::endl;
    std::cout << "  V           - 切换音频可视化" << std::endl;
    std::cout << "  I           - 切换信息面板" << std::endl;
    std::cout << "  L           - 切换播放列表" << std::endl;
    std::cout << "  1/2/3/4     - 播放速度 0.5x/1.0x/1.5x/2.0x" << std::endl;
    std::cout << "  F           - 切换收藏" << std::endl;
    std::cout << "  ESC         - 退出" << std::endl;
    std::cout << "  鼠标滚轮    - 音量控制" << std::endl;
    std::cout << "  点击进度条   - 跳转播放" << std::endl;
    std::cout << "==========================================\n" << std::endl;

    // 主循环
    while (!glfwWindowShouldClose(window_)) {
        handleInput();
        render();
        glfwSwapBuffers(window_);
        glfwPollEvents();
    }
}

void Application::cleanup()
{
    // 停止播放
    if (player_) player_->stop();

    // 释放UI资源
    if (ui_vao_) { glDeleteVertexArrays(1, &ui_vao_); ui_vao_ = 0; }
    if (ui_vbo_) { glDeleteBuffers(1, &ui_vbo_); ui_vbo_ = 0; }
    if (ui_ebo_) { glDeleteBuffers(1, &ui_ebo_); ui_ebo_ = 0; }
    if (ui_shader_program_) { glDeleteProgram(ui_shader_program_); ui_shader_program_ = 0; }

    // 释放组件（unique_ptr自动释放）
    player_.reset();
    renderer_.reset();
    visualizer_.reset();
    database_.reset();
    transcoder_.reset();

    // 销毁窗口
    if (window_) {
        glfwDestroyWindow(window_);
        window_ = nullptr;
    }
    glfwTerminate();
}

void Application::setInitialFiles(const std::vector<std::string>& files)
{
    if (files.empty()) return;

    player_->setPlaylist(files);
    if (player_->loadFile(files[0])) {
        player_->play();
    }
}

bool Application::connectDatabase(const std::string& host, const std::string& user,
                                   const std::string& password, const std::string& database)
{
    if (database_->connect(host, user, password, database)) {
        if (database_->initTables()) {
            db_connected_ = true;
            std::cout << "[App] 数据库连接成功" << std::endl;
            return true;
        }
    }
    std::cerr << "[App] 数据库连接失败" << std::endl;
    return false;
}

// ============================================================
// 渲染
// ============================================================
void Application::render()
{
    // 清除帧缓冲区
    glClearColor(0.08f, 0.08f, 0.12f, 1.0f);
    glClear(GL_COLOR_BUFFER_BIT);

    // 根据当前模式渲染
    switch (current_mode_) {
        case AppMode::Player:
            renderVideo();
            break;
        case AppMode::Visualizer:
            renderVisualizer();
            break;
        case AppMode::Library:
            // 媒体库模式（简化实现）
            break;
        case AppMode::Transcoder:
            // 转码模式（简化实现）
            break;
    }

    // 始终渲染控制面板
    renderControls();

    // 信息面板
    if (show_info_) {
        renderInfo();
    }

    // 模式指示器
    renderModeIndicator();
}

void Application::renderVideo()
{
    if (!player_->isPlaying() && !player_->isPaused()) {
        // 没有播放时显示提示
        int video_h = window_height_ - CONTROL_HEIGHT;
        glViewport(0, CONTROL_HEIGHT, window_width_, video_h);
        glClearColor(0.05f, 0.05f, 0.08f, 1.0f);
        glClear(GL_COLOR_BUFFER_BIT);
        return;
    }

    int video_h = window_height_ - CONTROL_HEIGHT;
    if (show_info_) {
        video_h = window_height_ - CONTROL_HEIGHT;
        int video_w = window_width_ - INFO_WIDTH;
        renderer_->setViewport(0, CONTROL_HEIGHT, video_w, video_h);
    }
    else {
        renderer_->setViewport(0, CONTROL_HEIGHT, window_width_, video_h);
    }
}

void Application::renderControls()
{
    // 底部控制面板区域
    glViewport(0, 0, window_width_, CONTROL_HEIGHT);
    glClearColor(0.12f, 0.12f, 0.18f, 1.0f);
    glClear(GL_COLOR_BUFFER_BIT);

    renderProgressBar();
}

void Application::renderProgressBar()
{
    double current_time = player_->getCurrentTime();
    double duration = player_->getDuration();

    if (duration <= 0) return;

    float progress = (float)(current_time / duration);

    // 进度条位置（像素坐标 → NDC坐标）
    int bar_x = 50;
    int bar_y = 60;
    int bar_width = window_width_ - 100;
    int bar_height = 8;

    float x1 = (2.0f * bar_x) / window_width_ - 1.0f;
    float y1 = (2.0f * bar_y) / CONTROL_HEIGHT - 1.0f;
    float x2 = (2.0f * (bar_x + bar_width)) / window_width_ - 1.0f;
    float y2 = (2.0f * (bar_y + bar_height)) / CONTROL_HEIGHT - 1.0f;

    glUseProgram(ui_shader_program_);

    // 背景条
    float bg_verts[] = { x1, y1, x2, y1, x2, y2, x1, y2 };
    unsigned int indices[] = { 0, 1, 2, 0, 2, 3 };

    glUniform3f(glGetUniformLocation(ui_shader_program_, "uColor"), 0.3f, 0.3f, 0.35f);
    glBindVertexArray(ui_vao_);
    glBindBuffer(GL_ARRAY_BUFFER, ui_vbo_);
    glBufferData(GL_ARRAY_BUFFER, sizeof(bg_verts), bg_verts, GL_DYNAMIC_DRAW);
    glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, ui_ebo_);
    glBufferData(GL_ELEMENT_ARRAY_BUFFER, sizeof(indices), indices, GL_DYNAMIC_DRAW);
    glDrawElements(GL_TRIANGLES, 6, GL_UNSIGNED_INT, 0);

    // 进度条
    float px2 = (2.0f * (bar_x + (int)(bar_width * progress))) / window_width_ - 1.0f;
    float prog_verts[] = { x1, y1, px2, y1, px2, y2, x1, y2 };

    glUniform3f(glGetUniformLocation(ui_shader_program_, "uColor"), 0.2f, 0.5f, 1.0f);
    glBufferData(GL_ARRAY_BUFFER, sizeof(prog_verts), prog_verts, GL_DYNAMIC_DRAW);
    glDrawElements(GL_TRIANGLES, 6, GL_UNSIGNED_INT, 0);

    glBindVertexArray(0);
    glUseProgram(0);
}

void Application::renderInfo()
{
    if (!player_->isPlaying() && !player_->isPaused()) return;

    // 右侧信息面板
    glViewport(window_width_ - INFO_WIDTH, CONTROL_HEIGHT, INFO_WIDTH,
               window_height_ - CONTROL_HEIGHT);
    glClearColor(0.1f, 0.1f, 0.15f, 1.0f);
    glClear(GL_COLOR_BUFFER_BIT);

    // 信息面板使用简化渲染（实际项目中可用文字渲染库如FreeType）
}

void Application::renderVisualizer()
{
    int viz_h = window_height_ - CONTROL_HEIGHT;
    visualizer_->render(window_width_, viz_h);
}

void Application::renderModeIndicator()
{
    // 顶部小条显示当前模式
    glViewport(0, window_height_ - 30, 200, 30);
    glClearColor(0.15f, 0.3f, 0.6f, 1.0f);
    glClear(GL_COLOR_BUFFER_BIT);
}

// ============================================================
// 输入处理
// ============================================================
void Application::handleInput()
{
    // SPACE - 播放/暂停
    if (glfwGetKey(window_, GLFW_KEY_SPACE) == GLFW_PRESS) {
        player_->togglePlayPause();
        glfwWaitEventsTimeout(0.2);
    }

    // RIGHT - 前进5秒
    if (glfwGetKey(window_, GLFW_KEY_RIGHT) == GLFW_PRESS) {
        player_->seek(player_->getCurrentTime() + 5.0);
        glfwWaitEventsTimeout(0.2);
    }

    // LEFT - 后退5秒
    if (glfwGetKey(window_, GLFW_KEY_LEFT) == GLFW_PRESS) {
        player_->seek(player_->getCurrentTime() - 5.0);
        glfwWaitEventsTimeout(0.2);
    }

    // UP - 音量增大
    if (glfwGetKey(window_, GLFW_KEY_UP) == GLFW_PRESS) {
        player_->setVolume(player_->getVolume() + 0.1f);
        std::cout << "[音量] " << (int)(player_->getVolume() * 100) << "%" << std::endl;
        glfwWaitEventsTimeout(0.2);
    }

    // DOWN - 音量减小
    if (glfwGetKey(window_, GLFW_KEY_DOWN) == GLFW_PRESS) {
        player_->setVolume(player_->getVolume() - 0.1f);
        std::cout << "[音量] " << (int)(player_->getVolume() * 100) << "%" << std::endl;
        glfwWaitEventsTimeout(0.2);
    }

    // N - 下一曲
    if (glfwGetKey(window_, GLFW_KEY_N) == GLFW_PRESS) {
        player_->nextFile();
        glfwWaitEventsTimeout(0.2);
    }

    // P - 上一曲
    if (glfwGetKey(window_, GLFW_KEY_P) == GLFW_PRESS) {
        player_->prevFile();
        glfwWaitEventsTimeout(0.2);
    }

    // E - 切换视频特效
    if (glfwGetKey(window_, GLFW_KEY_E) == GLFW_PRESS) {
        VideoEffect current = player_->getEffect();
        int next = (static_cast<int>(current) + 1) % static_cast<int>(VideoEffect::Count);
        player_->setEffect(static_cast<VideoEffect>(next));
        std::cout << "[特效] " << videoEffectToString(static_cast<VideoEffect>(next)) << std::endl;
        glfwWaitEventsTimeout(0.2);
    }

    // V - 切换可视化
    if (glfwGetKey(window_, GLFW_KEY_V) == GLFW_PRESS) {
        player_->toggleVisualizer();
        if (player_->isVisualizerShown()) {
            current_mode_ = AppMode::Visualizer;
        }
        else {
            current_mode_ = AppMode::Player;
        }
        std::cout << "[模式] " << (player_->isVisualizerShown() ? "可视化" : "播放器") << std::endl;
        glfwWaitEventsTimeout(0.2);
    }

    // I - 切换信息面板
    if (glfwGetKey(window_, GLFW_KEY_I) == GLFW_PRESS) {
        show_info_ = !show_info_;
        glfwWaitEventsTimeout(0.2);
    }

    // L - 切换播放列表
    if (glfwGetKey(window_, GLFW_KEY_L) == GLFW_PRESS) {
        show_playlist_ = !show_playlist_;
        glfwWaitEventsTimeout(0.2);
    }

    // 1/2/3/4 - 播放速度
    if (glfwGetKey(window_, GLFW_KEY_1) == GLFW_PRESS) {
        player_->setSpeed(0.5f);
        std::cout << "[速度] 0.5x" << std::endl;
        glfwWaitEventsTimeout(0.2);
    }
    if (glfwGetKey(window_, GLFW_KEY_2) == GLFW_PRESS) {
        player_->setSpeed(1.0f);
        std::cout << "[速度] 1.0x" << std::endl;
        glfwWaitEventsTimeout(0.2);
    }
    if (glfwGetKey(window_, GLFW_KEY_3) == GLFW_PRESS) {
        player_->setSpeed(1.5f);
        std::cout << "[速度] 1.5x" << std::endl;
        glfwWaitEventsTimeout(0.2);
    }
    if (glfwGetKey(window_, GLFW_KEY_4) == GLFW_PRESS) {
        player_->setSpeed(2.0f);
        std::cout << "[速度] 2.0x" << std::endl;
        glfwWaitEventsTimeout(0.2);
    }

    // F - 收藏切换
    if (glfwGetKey(window_, GLFW_KEY_F) == GLFW_PRESS) {
        if (db_connected_) {
            const MediaInfo& info = player_->getMediaInfo();
            if (info.db_id > 0) {
                database_->setFavorite(info.db_id, !info.is_favorite);
                std::cout << "[收藏] " << (info.is_favorite ? "取消" : "添加") << std::endl;
            }
        }
        glfwWaitEventsTimeout(0.2);
    }
}

// ============================================================
// GLFW回调函数
// ============================================================
void Application::keyCallback(GLFWwindow* window, int key, int scancode, int action, int mods)
{
    Application* app = static_cast<Application*>(glfwGetWindowUserPointer(window));

    if (key == GLFW_KEY_ESCAPE && action == GLFW_PRESS) {
        glfwSetWindowShouldClose(window, GLFW_TRUE);
    }
}

void Application::mouseButtonCallback(GLFWwindow* window, int button, int action, int mods)
{
    Application* app = static_cast<Application*>(glfwGetWindowUserPointer(window));

    if (button == GLFW_MOUSE_BUTTON_LEFT && action == GLFW_PRESS) {
        double xpos, ypos;
        glfwGetCursorPos(window, &xpos, &ypos);

        // 检查是否点击了进度条区域
        int bar_x = 50;
        int bar_y = 60;
        int bar_width = app->window_width_ - 100;
        int bar_height = 8;

        if (xpos >= bar_x && xpos <= bar_x + bar_width &&
            ypos >= bar_y && ypos <= bar_y + bar_height) {
            double duration = app->player_->getDuration();
            if (duration > 0) {
                double seek_pos = ((xpos - bar_x) / bar_width) * duration;
                app->player_->seek(seek_pos);
            }
        }
    }
}

void Application::scrollCallback(GLFWwindow* window, double xoffset, double yoffset)
{
    Application* app = static_cast<Application*>(glfwGetWindowUserPointer(window));
    float volume = app->player_->getVolume();
    volume += (float)yoffset * 0.05f;
    app->player_->setVolume(volume);
}

void Application::framebufferSizeCallback(GLFWwindow* window, int width, int height)
{
    Application* app = static_cast<Application*>(glfwGetWindowUserPointer(window));
    app->window_width_ = width;
    app->window_height_ = height;
    glViewport(0, 0, width, height);
}

void Application::windowCloseCallback(GLFWwindow* window)
{
    // 窗口关闭时的清理
}

// ============================================================
// UI着色器初始化
// ============================================================
bool Application::initUIShader()
{
    GLuint vs = glCreateShader(GL_VERTEX_SHADER);
    glShaderSource(vs, 1, &ui_vertex_shader, nullptr);
    glCompileShader(vs);

    GLint success;
    glGetShaderiv(vs, GL_COMPILE_STATUS, &success);
    if (!success) {
        char info[512];
        glGetShaderInfoLog(vs, 512, nullptr, info);
        std::cerr << "[App] UI顶点着色器编译失败: " << info << std::endl;
        return false;
    }

    GLuint fs = glCreateShader(GL_FRAGMENT_SHADER);
    glShaderSource(fs, 1, &ui_fragment_shader, nullptr);
    glCompileShader(fs);

    glGetShaderiv(fs, GL_COMPILE_STATUS, &success);
    if (!success) {
        char info[512];
        glGetShaderInfoLog(fs, 512, nullptr, info);
        std::cerr << "[App] UI片段着色器编译失败: " << info << std::endl;
        glDeleteShader(vs);
        return false;
    }

    ui_shader_program_ = glCreateProgram();
    glAttachShader(ui_shader_program_, vs);
    glAttachShader(ui_shader_program_, fs);
    glLinkProgram(ui_shader_program_);

    glGetProgramiv(ui_shader_program_, GL_LINK_STATUS, &success);
    if (!success) {
        char info[512];
        glGetProgramInfoLog(ui_shader_program_, 512, nullptr, info);
        std::cerr << "[App] UI着色器链接失败: " << info << std::endl;
    }

    glDeleteShader(vs);
    glDeleteShader(fs);
    return success;
}

void Application::initUIGeometry()
{
    glGenVertexArrays(1, &ui_vao_);
    glGenBuffers(1, &ui_vbo_);
    glGenBuffers(1, &ui_ebo_);

    glBindVertexArray(ui_vao_);
    glBindBuffer(GL_ARRAY_BUFFER, ui_vbo_);
    // 位置属性：2个float分量
    glVertexAttribPointer(0, 2, GL_FLOAT, GL_FALSE, 2 * sizeof(float), (void*)0);
    glEnableVertexAttribArray(0);
    glBindBuffer(GL_ARRAY_BUFFER, 0);
    glBindVertexArray(0);
}
