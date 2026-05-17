/**
 * @file VideoRenderer.cpp
 * @brief 视频渲染器实现 - OpenGL渲染与特效着色器
 */

#include "VideoRenderer.h"
#include <iostream>

// ============================================================
// 着色器源码
// ============================================================

/**
 * 顶点着色器（Vertex Shader）
 * 
 * 作用：处理每个顶点的位置和属性
 * - 接收顶点坐标(aPos)和纹理坐标(aTexCoord)
 * - 将顶点坐标转换为裁剪空间坐标（gl_Position）
 * - 将纹理坐标传递给片段着色器（TexCoord）
 * 
 * GLSL语法要点：
 * - layout(location=N)：指定属性位置
 * - in/out：输入/输出变量
 * - vec3/vec2：3维/2维向量
 * - gl_Position：内建输出变量（顶点裁剪空间坐标）
 */
static const char* vertex_shader_source = R"(
#version 330 core
layout (location = 0) in vec3 aPos;       // 顶点位置（NDC坐标）
layout (location = 1) in vec2 aTexCoord;   // 纹理坐标（0~1范围）
out vec2 TexCoord;                          // 传递给片段着色器的纹理坐标

void main() {
    gl_Position = vec4(aPos, 1.0);  // 直接使用NDC坐标，无需变换矩阵
    TexCoord = aTexCoord;            // 纹理坐标原样传递
}
)";

/**
 * 片段着色器（Fragment Shader）- 包含所有特效
 * 
 * 作用：计算每个像素的最终颜色
 * - 从纹理采样视频帧颜色
 * - 根据uEffect选择不同的特效处理
 * - 输出最终像素颜色(FragColor)
 * 
 * 特效算法说明：
 * 1. 灰度(Grayscale)：使用人眼感知权重 (0.299, 0.587, 0.114)
 * 2. 反色(Invert)：1.0 - 原色
 * 3. 边缘检测(EdgeDetect)：Sobel算子，检测亮度梯度
 * 4. 模糊(Blur)：9点盒式模糊（3x3核）
 * 5. 亮度/对比度：color = (color - 0.5) * contrast + 0.5 + brightness
 * 6. 怀旧色调(Sepia)：应用棕色调变换矩阵
 * 
 * uniform变量说明：
 * - texture1：视频帧纹理
 * - uEffect：特效选择（0=无, 1=灰度, 2=反色, 3=边缘, 4=模糊, 5=亮度对比度, 6=怀旧）
 * - uBrightness：亮度偏移
 * - uContrast：对比度倍率
 * - uTexelSize：单个纹素大小（1/width, 1/height），用于采样偏移
 */
static const char* fragment_shader_source = R"(
#version 330 core
out vec4 FragColor;
in vec2 TexCoord;

uniform sampler2D texture1;     // RGBA纹理
uniform sampler2D textureY;     // Y平面纹理
uniform sampler2D textureU;     // U平面纹理
uniform sampler2D textureV;     // V平面纹理
uniform int uInputFormat;       // 0=RGBA, 1=YUV420P
uniform bool uFullRange;
uniform bool uBt709;
uniform int uEffect;            // 特效选择
uniform float uBrightness;     // 亮度参数
uniform float uContrast;       // 对比度参数
uniform vec2 uTexelSize;       // 纹素大小（用于邻域采样）

vec4 sampleVideo(vec2 coord) {
    if (uInputFormat == 1) {
        float y = texture(textureY, coord).r;
        float u = texture(textureU, coord).r - 0.5;
        float v = texture(textureV, coord).r - 0.5;
        vec3 rgb;
        if (uFullRange && uBt709) {
            rgb = vec3(
                y + 1.5748 * v,
                y - 0.187324 * u - 0.468124 * v,
                y + 1.8556 * u
            );
        }
        else if (uFullRange) {
            rgb = vec3(
                y + 1.402 * v,
                y - 0.344136 * u - 0.714136 * v,
                y + 1.772 * u
            );
        }
        else if (uBt709) {
            y = 1.16438356 * (y - 0.0627451);
            rgb = vec3(
                y + 1.792741 * v,
                y - 0.213249 * u - 0.532909 * v,
                y + 2.112402 * u
            );
        }
        else {
            y = 1.16438356 * (y - 0.0627451);
            rgb = vec3(
                y + 1.596027 * v,
                y - 0.391762 * u - 0.812968 * v,
                y + 2.017232 * u
            );
        }
        return vec4(clamp(rgb, 0.0, 1.0), 1.0);
    }
    return texture(texture1, coord);
}

void main() {
    vec4 color = sampleVideo(TexCoord);

    // 无特效 - 直接输出原始颜色
    if (uEffect == 0) {
        FragColor = color;
    }
    // 灰度效果 - 将RGB转为亮度值
    // 使用ITU-R BT.601标准的感知权重
    else if (uEffect == 1) {
        float gray = dot(color.rgb, vec3(0.299, 0.587, 0.114));
        FragColor = vec4(vec3(gray), 1.0);
    }
    // 反色效果 - 颜色取反
    else if (uEffect == 2) {
        FragColor = vec4(1.0 - color.rgb, 1.0);
    }
    // 边缘检测 - Sobel算子
    // 计算水平和垂直方向的亮度梯度，合成边缘强度
    else if (uEffect == 3) {
        float tl = dot(sampleVideo(TexCoord + vec2(-uTexelSize.x,  uTexelSize.y)).rgb, vec3(0.299, 0.587, 0.114));
        float t  = dot(sampleVideo(TexCoord + vec2( 0.0,           uTexelSize.y)).rgb, vec3(0.299, 0.587, 0.114));
        float tr = dot(sampleVideo(TexCoord + vec2( uTexelSize.x,  uTexelSize.y)).rgb, vec3(0.299, 0.587, 0.114));
        float l  = dot(sampleVideo(TexCoord + vec2(-uTexelSize.x,  0.0)).rgb, vec3(0.299, 0.587, 0.114));
        float r  = dot(sampleVideo(TexCoord + vec2( uTexelSize.x,  0.0)).rgb, vec3(0.299, 0.587, 0.114));
        float bl = dot(sampleVideo(TexCoord + vec2(-uTexelSize.x, -uTexelSize.y)).rgb, vec3(0.299, 0.587, 0.114));
        float b  = dot(sampleVideo(TexCoord + vec2( 0.0,          -uTexelSize.y)).rgb, vec3(0.299, 0.587, 0.114));
        float br = dot(sampleVideo(TexCoord + vec2( uTexelSize.x, -uTexelSize.y)).rgb, vec3(0.299, 0.587, 0.114));

        // Sobel水平核: [-1,0,1; -2,0,2; -1,0,1]
        float gx = -tl - 2.0*l - bl + tr + 2.0*r + br;
        // Sobel垂直核: [-1,-2,-1; 0,0,0; 1,2,1]
        float gy = -tl - 2.0*t - tr + bl + 2.0*b + br;
        float edge = sqrt(gx*gx + gy*gy);
        FragColor = vec4(vec3(edge), 1.0);
    }
    // 盒式模糊 - 3x3邻域平均
    else if (uEffect == 4) {
        vec3 sum = vec3(0.0);
        for (int x = -1; x <= 1; x++) {
            for (int y = -1; y <= 1; y++) {
                sum += sampleVideo(TexCoord + vec2(x, y) * uTexelSize).rgb;
            }
        }
        FragColor = vec4(sum / 9.0, 1.0);
    }
    // 亮度/对比度调节
    // 对比度：以0.5为中心缩放颜色范围
    // 亮度：整体偏移
    else if (uEffect == 5) {
        vec3 result = (color.rgb - 0.5) * uContrast + 0.5 + uBrightness;
        FragColor = vec4(clamp(result, 0.0, 1.0), 1.0);
    }
    // 怀旧色调(Sepia) - 模拟老照片的棕色调
    // 使用颜色变换矩阵将RGB转为棕色调
    else if (uEffect == 6) {
        vec3 sepia;
        sepia.r = dot(color.rgb, vec3(0.393, 0.769, 0.189));
        sepia.g = dot(color.rgb, vec3(0.349, 0.686, 0.168));
        sepia.b = dot(color.rgb, vec3(0.272, 0.534, 0.131));
        FragColor = vec4(clamp(sepia, 0.0, 1.0), 1.0);
    }
    else {
        FragColor = color;
    }
}
)";

// ============================================================
// 构造/析构
// ============================================================
VideoRenderer::VideoRenderer()
    : vao_(0)
    , vbo_(0)
    , ebo_(0)
    , shader_program_(0)
    , texture_width_(0)
    , texture_height_(0)
    , texture_format_(VideoFrameFormat::RGBA)
    , current_effect_(VideoEffect::None)
    , brightness_(0.0f)
    , contrast_(1.0f)
{
    textures_[0] = 0;
    textures_[1] = 0;
    textures_[2] = 0;
}

VideoRenderer::~VideoRenderer()
{
    cleanup();
}

// ============================================================
// 初始化
// ============================================================
bool VideoRenderer::init(int width, int height)
{
    texture_width_ = width;
    texture_height_ = height;

    if (!compileShaders()) return false;
    if (!createTextures()) return false;
    createGeometry();

    std::cout << "[VideoRenderer] 初始化成功: " << width << "x" << height << std::endl;
    return true;
}

void VideoRenderer::cleanup()
{
    if (vao_) { glDeleteVertexArrays(1, &vao_); vao_ = 0; }
    if (vbo_) { glDeleteBuffers(1, &vbo_); vbo_ = 0; }
    if (ebo_) { glDeleteBuffers(1, &ebo_); ebo_ = 0; }
    glDeleteTextures(3, textures_);
    textures_[0] = 0;
    textures_[1] = 0;
    textures_[2] = 0;
    if (shader_program_) { glDeleteProgram(shader_program_); shader_program_ = 0; }
}

// ============================================================
// 编译着色器
// ============================================================
bool VideoRenderer::compileShaders()
{
    // --------------------------------------------------------
    // 编译顶点着色器
    // glCreateShader() 创建着色器对象
    // glShaderSource() 设置着色器源码
    // glCompileShader() 编译着色器
    // glGetShaderiv() 查询编译状态
    // --------------------------------------------------------
    GLuint vertex_shader = glCreateShader(GL_VERTEX_SHADER);
    glShaderSource(vertex_shader, 1, &vertex_shader_source, nullptr);
    glCompileShader(vertex_shader);

    GLint success;
    glGetShaderiv(vertex_shader, GL_COMPILE_STATUS, &success);
    if (!success) {
        char info_log[512];
        glGetShaderInfoLog(vertex_shader, 512, nullptr, info_log);
        std::cerr << "[VideoRenderer] 顶点着色器编译失败: " << info_log << std::endl;
        return false;
    }

    // --------------------------------------------------------
    // 编译片段着色器
    // --------------------------------------------------------
    GLuint fragment_shader = glCreateShader(GL_FRAGMENT_SHADER);
    glShaderSource(fragment_shader, 1, &fragment_shader_source, nullptr);
    glCompileShader(fragment_shader);

    glGetShaderiv(fragment_shader, GL_COMPILE_STATUS, &success);
    if (!success) {
        char info_log[512];
        glGetShaderInfoLog(fragment_shader, 512, nullptr, info_log);
        std::cerr << "[VideoRenderer] 片段着色器编译失败: " << info_log << std::endl;
        glDeleteShader(vertex_shader);
        return false;
    }

    // --------------------------------------------------------
    // 链接着色器程序
    // 将顶点着色器和片段着色器连接成完整的渲染管线
    // --------------------------------------------------------
    shader_program_ = glCreateProgram();
    glAttachShader(shader_program_, vertex_shader);
    glAttachShader(shader_program_, fragment_shader);
    glLinkProgram(shader_program_);

    glGetProgramiv(shader_program_, GL_LINK_STATUS, &success);
    if (!success) {
        char info_log[512];
        glGetProgramInfoLog(shader_program_, 512, nullptr, info_log);
        std::cerr << "[VideoRenderer] 着色器程序链接失败: " << info_log << std::endl;
        glDeleteShader(vertex_shader);
        glDeleteShader(fragment_shader);
        return false;
    }

    // 着色器已链接到程序中，可以删除着色器对象
    glDeleteShader(vertex_shader);
    glDeleteShader(fragment_shader);

    return true;
}

// ============================================================
// 创建纹理
// ============================================================
bool VideoRenderer::createTextures()
{
    glGenTextures(3, textures_);
    for (GLuint texture : textures_) {
        glBindTexture(GL_TEXTURE_2D, texture);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    }

    glBindTexture(GL_TEXTURE_2D, 0);
    texture_width_ = 0;
    texture_height_ = 0;
    return true;
}

// ============================================================
// 创建几何体 - 全屏四边形
// ============================================================
void VideoRenderer::createGeometry()
{
    // 全屏四边形的顶点数据
    // 使用归一化设备坐标(NDC)：范围[-1,1]
    // 纹理坐标：范围[0,1]，(0,0)=左下角，(1,1)=右上角
    // 
    // 注意：Y轴纹理坐标翻转（1.0在上方），因为视频帧的Y轴方向
    // 与OpenGL的Y轴方向相反（视频帧从上到下，OpenGL从下到上）
    float vertices[] = {
        // 位置(x,y,z)          // 纹理坐标(s,t)
         1.0f,  1.0f, 0.0f,    1.0f, 0.0f,   // 右上
         1.0f, -1.0f, 0.0f,    1.0f, 1.0f,   // 右下
        -1.0f, -1.0f, 0.0f,    0.0f, 1.0f,   // 左下
        -1.0f,  1.0f, 0.0f,    0.0f, 0.0f,   // 左上
    };

    // 索引数据：两个三角形组成一个四边形
    // 使用索引可以减少顶点数据量（4个顶点 vs 6个顶点）
    unsigned int indices[] = {
        0, 1, 3,   // 第一个三角形
        1, 2, 3    // 第二个三角形
    };

    // 创建OpenGL对象
    glGenVertexArrays(1, &vao_);
    glGenBuffers(1, &vbo_);
    glGenBuffers(1, &ebo_);

    // 绑定VAO（记录后续的顶点属性配置）
    glBindVertexArray(vao_);

    // 绑定VBO并上传顶点数据
    // GL_ARRAY_BUFFER：顶点数据缓冲区类型
    // GL_STATIC_DRAW：数据不会改变，GPU可以优化存储位置
    glBindBuffer(GL_ARRAY_BUFFER, vbo_);
    glBufferData(GL_ARRAY_BUFFER, sizeof(vertices), vertices, GL_STATIC_DRAW);

    // 绑定EBO并上传索引数据
    // GL_ELEMENT_ARRAY_BUFFER：索引数据缓冲区类型
    glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, ebo_);
    glBufferData(GL_ELEMENT_ARRAY_BUFFER, sizeof(indices), indices, GL_STATIC_DRAW);

    // 设置顶点属性指针
    // 告诉OpenGL如何解析VBO中的顶点数据
    // 
    // 属性0：位置(aPos)
    // - 3个分量(GL_FLOAT)
    // - 步长5个float（位置3+纹理2）
    // - 偏移0字节
    glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, 5 * sizeof(float), (void*)0);
    glEnableVertexAttribArray(0);

    // 属性1：纹理坐标(aTexCoord)
    // - 2个分量(GL_FLOAT)
    // - 步长5个float
    // - 偏移3个float（跳过位置数据）
    glVertexAttribPointer(1, 2, GL_FLOAT, GL_FALSE, 5 * sizeof(float), (void*)(3 * sizeof(float)));
    glEnableVertexAttribArray(1);

    // 解绑（VAO已记录了所有配置）
    glBindBuffer(GL_ARRAY_BUFFER, 0);
    glBindVertexArray(0);
}

// ============================================================
// 渲染一帧
// ============================================================
void VideoRenderer::ensureTextureStorage(VideoFrameFormat format, int width, int height)
{
    if (format == texture_format_ && width == texture_width_ && height == texture_height_) {
        return;
    }

    texture_format_ = format;
    texture_width_ = width;
    texture_height_ = height;

    if (format == VideoFrameFormat::YUV420P) {
        const int chroma_width = (width + 1) / 2;
        const int chroma_height = (height + 1) / 2;
        glBindTexture(GL_TEXTURE_2D, textures_[0]);
        glTexImage2D(GL_TEXTURE_2D, 0, GL_R8, width, height, 0, GL_RED, GL_UNSIGNED_BYTE, nullptr);
        glBindTexture(GL_TEXTURE_2D, textures_[1]);
        glTexImage2D(GL_TEXTURE_2D, 0, GL_R8, chroma_width, chroma_height, 0, GL_RED, GL_UNSIGNED_BYTE, nullptr);
        glBindTexture(GL_TEXTURE_2D, textures_[2]);
        glTexImage2D(GL_TEXTURE_2D, 0, GL_R8, chroma_width, chroma_height, 0, GL_RED, GL_UNSIGNED_BYTE, nullptr);
    }
    else {
        glBindTexture(GL_TEXTURE_2D, textures_[0]);
        glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, width, height, 0, GL_RGBA, GL_UNSIGNED_BYTE, nullptr);
    }
    glBindTexture(GL_TEXTURE_2D, 0);
}

void VideoRenderer::renderRGBA(const uint8_t* data, int width, int height)
{
    ensureTextureStorage(VideoFrameFormat::RGBA, width, height);
    glPixelStorei(GL_UNPACK_ALIGNMENT, 4);
    glActiveTexture(GL_TEXTURE0);
    glBindTexture(GL_TEXTURE_2D, textures_[0]);
    glTexSubImage2D(GL_TEXTURE_2D, 0, 0, 0, width, height, GL_RGBA, GL_UNSIGNED_BYTE, data);
    drawFrame(VideoFrameFormat::RGBA);
}

void VideoRenderer::renderYUV420P(const uint8_t* y, const uint8_t* u, const uint8_t* v,
    bool full_range, bool bt709, int width, int height)
{
    ensureTextureStorage(VideoFrameFormat::YUV420P, width, height);
    const int chroma_width = (width + 1) / 2;
    const int chroma_height = (height + 1) / 2;
    glPixelStorei(GL_UNPACK_ALIGNMENT, 1);

    glActiveTexture(GL_TEXTURE0);
    glBindTexture(GL_TEXTURE_2D, textures_[0]);
    glTexSubImage2D(GL_TEXTURE_2D, 0, 0, 0, width, height, GL_RED, GL_UNSIGNED_BYTE, y);
    glActiveTexture(GL_TEXTURE1);
    glBindTexture(GL_TEXTURE_2D, textures_[1]);
    glTexSubImage2D(GL_TEXTURE_2D, 0, 0, 0, chroma_width, chroma_height, GL_RED, GL_UNSIGNED_BYTE, u);
    glActiveTexture(GL_TEXTURE2);
    glBindTexture(GL_TEXTURE_2D, textures_[2]);
    glTexSubImage2D(GL_TEXTURE_2D, 0, 0, 0, chroma_width, chroma_height, GL_RED, GL_UNSIGNED_BYTE, v);
    glUseProgram(shader_program_);
    glUniform1i(glGetUniformLocation(shader_program_, "uFullRange"), full_range ? 1 : 0);
    glUniform1i(glGetUniformLocation(shader_program_, "uBt709"), bt709 ? 1 : 0);
    drawFrame(VideoFrameFormat::YUV420P);
    glActiveTexture(GL_TEXTURE0);
}

void VideoRenderer::drawFrame(VideoFrameFormat format)
{
    glUseProgram(shader_program_);
    glUniform1i(glGetUniformLocation(shader_program_, "texture1"), 0);
    glUniform1i(glGetUniformLocation(shader_program_, "textureY"), 0);
    glUniform1i(glGetUniformLocation(shader_program_, "textureU"), 1);
    glUniform1i(glGetUniformLocation(shader_program_, "textureV"), 2);
    glUniform1i(glGetUniformLocation(shader_program_, "uInputFormat"),
        format == VideoFrameFormat::YUV420P ? 1 : 0);

    glUniform1i(glGetUniformLocation(shader_program_, "uEffect"), static_cast<int>(current_effect_));
    glUniform1f(glGetUniformLocation(shader_program_, "uBrightness"), brightness_);
    glUniform1f(glGetUniformLocation(shader_program_, "uContrast"), contrast_);

    if (texture_width_ > 0 && texture_height_ > 0) {
        glUniform2f(glGetUniformLocation(shader_program_, "uTexelSize"),
            1.0f / texture_width_, 1.0f / texture_height_);
    }

    glBindVertexArray(vao_);
    glDrawElements(GL_TRIANGLES, 6, GL_UNSIGNED_INT, 0);
    glBindVertexArray(0);
    glBindTexture(GL_TEXTURE_2D, 0);
}

void VideoRenderer::resize(int width, int height)
{
    glViewport(0, 0, width, height);
}

void VideoRenderer::setViewport(int x, int y, int width, int height)
{
    glViewport(x, y, width, height);
}

void VideoRenderer::setEffect(VideoEffect effect)
{
    current_effect_ = effect;
    std::cout << "[VideoRenderer] 特效切换: " << videoEffectToString(effect) << std::endl;
}
