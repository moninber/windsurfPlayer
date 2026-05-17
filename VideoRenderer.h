/**
 * @file VideoRenderer.h
 * @brief 视频渲染器 - 基于OpenGL的视频渲染与特效处理
 * 
 * 本类实现了基于OpenGL的视频帧渲染，支持多种实时视频特效：
 * 
 * OpenGL渲染管线（Rendering Pipeline）：
 * 1. 顶点数据(VBO) → 顶点着色器 → 图元装配 → 光栅化 → 片段着色器 → 帧缓冲
 * 
 * 核心概念：
 * - VAO (Vertex Array Object)：存储顶点属性配置
 * - VBO (Vertex Buffer Object)：存储顶点数据（坐标、纹理坐标等）
 * - Shader Program：GPU上运行的程序（顶点+片段着色器）
 * - Texture：纹理对象，存储图像/视频帧数据
 * 
 * 特效实现原理：
 * - 所有特效通过片段着色器(Fragment Shader)实现
 * - 通过uniform变量控制特效参数
 * - GPU并行处理每个像素，实现实时特效
 * 
 * 学习要点：
 * - GLSL着色器语言
 * - OpenGL纹理上传与采样
 * - 帧缓冲区操作
 * - 着色器uniform变量传递
 */

#pragma once

#include <GL/glew.h>
#include <string>
#include "MediaInfo.h"

class VideoRenderer {
public:
    VideoRenderer();
    ~VideoRenderer();

    /**
     * @brief 初始化渲染器（创建着色器、纹理、几何体）
     * @param width 视频宽度
     * @param height 视频高度
     * @return true=成功, false=失败
     */
    bool init(int width, int height);

    /** @brief 释放OpenGL资源 */
    void cleanup();

    /**
     * @brief 渲染一帧视频数据
     * @param data RGBA像素数据
     * @param width 帧宽度
     * @param height 帧高度
     */
    void renderRGBA(const uint8_t* data, int width, int height);
    void renderYUV420P(const uint8_t* y, const uint8_t* u, const uint8_t* v,
        bool full_range, bool bt709, int width, int height);

    /** @brief 窗口大小变化时调整视口 */
    void resize(int width, int height);

    /** @brief 设置视口区域（用于分区域渲染） */
    void setViewport(int x, int y, int width, int height);

    /** @brief 切换视频特效 */
    void setEffect(VideoEffect effect);

    /** @brief 设置亮度参数（-1.0~1.0） */
    void setBrightness(float b) { brightness_ = b; }

    /** @brief 设置对比度参数（0.0~3.0） */
    void setContrast(float c) { contrast_ = c; }

    /** @brief 获取亮度 */
    float getBrightness() const { return brightness_; }

    /** @brief 获取对比度 */
    float getContrast() const { return contrast_; }

private:
    /**
     * @brief 编译着色器程序
     * 包含多种特效的片段着色器，通过uniform控制选择
     */
    bool compileShaders();

    /** @brief 创建纹理对象 */
    bool createTextures();
    void ensureTextureStorage(VideoFrameFormat format, int width, int height);
    void drawFrame(VideoFrameFormat format);

    /** @brief 创建顶点几何体（全屏四边形） */
    void createGeometry();

    GLuint vao_;               // 顶点数组对象
    GLuint vbo_;               // 顶点缓冲对象
    GLuint ebo_;               // 索引缓冲对象
    GLuint textures_[3];       // 视频纹理对象
    GLuint shader_program_;    // 着色器程序

    int texture_width_;        // 纹理宽度
    int texture_height_;       // 纹理高度
    VideoFrameFormat texture_format_;

    VideoEffect current_effect_;  // 当前特效
    float brightness_;            // 亮度参数
    float contrast_;              // 对比度参数
};
