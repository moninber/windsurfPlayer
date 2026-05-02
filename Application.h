/**
 * @file Application.h
 * @brief 应用程序主类 - GUI界面与交互逻辑
 * 
 * 本类管理整个应用的生命周期和用户交互：
 * - GLFW窗口创建与事件处理
 * - OpenGL上下文管理
 * - 键盘/鼠标输入处理
 * - UI渲染（进度条、控制面板、信息显示）
 * - 模式切换（播放器/可视化/媒体库/转码）
 * 
 * GLFW (Graphics Library Framework)：
 * - 轻量级的OpenGL窗口和输入管理库
 * - 跨平台支持（Windows/macOS/Linux）
 * - 提供窗口创建、输入处理、上下文管理
 * 
 * OpenGL上下文规则：
 * - OpenGL上下文必须在创建它的线程中使用
 * - GLEW初始化必须在创建OpenGL上下文之后
 * - 任何OpenGL调用都需要当前上下文
 * 
 * 学习要点：
 * - GLFW窗口管理
 * - OpenGL渲染管线
 * - 事件驱动编程模型
 * - UI设计与交互
 */

#pragma once

#include <GL/glew.h>
#include <GLFW/glfw3.h>
#include <string>
#include <vector>
#include <memory>

#include "MediaInfo.h"
#include "PlayerController.h"
#include "VideoRenderer.h"
#include "AudioVisualizer.h"
#include "MediaDatabase.h"
#include "MediaTranscoder.h"

class Application {
public:
    Application();
    ~Application();

    /**
     * @brief 初始化应用
     * @param width 窗口宽度
     * @param height 窗口高度
     * @return true=成功
     */
    bool init(int width = 1280, int height = 720);

    /** @brief 运行主循环 */
    void run();

    /** @brief 清理资源 */
    void cleanup();

    /**
     * @brief 设置初始播放文件
     * @param files 文件路径列表
     */
    void setInitialFiles(const std::vector<std::string>& files);

    /**
     * @brief 连接数据库
     * @param host 主机
     * @param user 用户名
     * @param password 密码
     * @param database 数据库名
     * @return true=成功
     */
    bool connectDatabase(const std::string& host, const std::string& user,
                         const std::string& password, const std::string& database);

private:
    // 渲染函数
    void render();
    void renderVideo();
    void renderControls();
    void renderProgressBar();
    void renderInfo();
    void renderVisualizer();
    void renderModeIndicator();

    // 输入处理
    void handleInput();
    void handleConsoleInput();

    // GLFW回调函数（静态，因为GLFW是C API）
    static void keyCallback(GLFWwindow* window, int key, int scancode, int action, int mods);
    static void mouseButtonCallback(GLFWwindow* window, int button, int action, int mods);
    static void scrollCallback(GLFWwindow* window, double xoffset, double yoffset);
    static void framebufferSizeCallback(GLFWwindow* window, int width, int height);
    static void windowCloseCallback(GLFWwindow* window);

    // 初始化UI着色器
    bool initUIShader();
    void initUIGeometry();

    // GLFW窗口
    GLFWwindow* window_;
    int window_width_;
    int window_height_;

    // 核心组件
    std::unique_ptr<PlayerController> player_;
    std::unique_ptr<VideoRenderer> renderer_;
    std::unique_ptr<AudioVisualizer> visualizer_;
    std::unique_ptr<MediaDatabase> database_;
    std::unique_ptr<MediaTranscoder> transcoder_;

    // UI着色器（用于绘制进度条等UI元素）
    GLuint ui_shader_program_;
    GLuint ui_vao_;
    GLuint ui_vbo_;
    GLuint ui_ebo_;

    // 应用状态
    AppMode current_mode_;
    bool show_playlist_;
    bool show_info_;
    bool db_connected_;
    bool console_input_enabled_;

    // 控制面板布局参数
    static constexpr int CONTROL_HEIGHT = 120;  // 底部控制面板高度
    static constexpr int INFO_WIDTH = 300;       // 右侧信息面板宽度
};
