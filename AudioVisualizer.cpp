/**
 * @file AudioVisualizer.cpp
 * @brief 音频可视化器实现 - FFT频谱分析与OpenGL渲染
 */

#include "AudioVisualizer.h"
#include <iostream>
#include <cmath>

#ifndef M_PI
#define M_PI 3.14159265358979323846
#endif

// ============================================================
// 频谱渲染着色器
// ============================================================

/**
 * 顶点着色器 - 频谱柱状图
 * 每个顶点包含：位置(x,y) 和 颜色(r,g,b)
 * 颜色根据频率位置渐变：低频=蓝色 → 中频=绿色 → 高频=红色
 */
static const char* viz_vertex_shader = R"(
#version 330 core
layout (location = 0) in vec2 aPos;    // 顶点位置
layout (location = 1) in vec3 aColor;   // 顶点颜色
out vec3 Color;

void main() {
    gl_Position = vec4(aPos, 0.0, 1.0);
    Color = aColor;
}
)";

/**
 * 片段着色器 - 简单颜色输出
 */
static const char* viz_fragment_shader = R"(
#version 330 core
in vec3 Color;
out vec4 FragColor;

void main() {
    FragColor = vec4(Color, 1.0);
}
)";

// ============================================================
// 构造/析构
// ============================================================
AudioVisualizer::AudioVisualizer()
    : shader_program_(0)
    , vao_(0)
    , vbo_(0)
    , bar_count_(64)
{
}

AudioVisualizer::~AudioVisualizer()
{
    cleanup();
}

// ============================================================
// 初始化
// ============================================================
bool AudioVisualizer::init(int bar_count, bool create_gl_resources)
{
    bar_count_ = bar_count;
    spectrum_data_.resize(bar_count_, 0.0f);
    smoothed_data_.resize(bar_count_, 0.0f);

    // --------------------------------------------------------
    // 预计算汉宁窗(Hanning Window)系数
    // 
    // 为什么需要窗函数？
    /** FFT假设输入信号是无限周期的，但实际只取有限长度的数据
    * 直接截断会产生频谱泄漏（spectral leakage）
    * 窗函数使信号两端平滑衰减到0，减少泄漏
    * 
    * 汉宁窗公式：w(n) = 0.5 * (1 - cos(2πn/(N-1)))*/
    // --------------------------------------------------------
    window_.resize(FFT_SIZE);
    for (int i = 0; i < FFT_SIZE; i++) {
        window_[i] = 0.5 * (1.0 - cos(2.0 * M_PI * i / (FFT_SIZE - 1)));
    }

    fft_input_.resize(FFT_SIZE);

    if (!create_gl_resources) {
        return true;
    }

    // --------------------------------------------------------
    // 编译着色器
    // --------------------------------------------------------
    GLuint vs = glCreateShader(GL_VERTEX_SHADER);
    glShaderSource(vs, 1, &viz_vertex_shader, nullptr);
    glCompileShader(vs);

    GLint success;
    glGetShaderiv(vs, GL_COMPILE_STATUS, &success);
    if (!success) {
        char info_log[512];
        glGetShaderInfoLog(vs, 512, nullptr, info_log);
        std::cerr << "[AudioVisualizer] 顶点着色器编译失败: " << info_log << std::endl;
        return false;
    }

    GLuint fs = glCreateShader(GL_FRAGMENT_SHADER);
    glShaderSource(fs, 1, &viz_fragment_shader, nullptr);
    glCompileShader(fs);

    glGetShaderiv(fs, GL_COMPILE_STATUS, &success);
    if (!success) {
        char info_log[512];
        glGetShaderInfoLog(fs, 512, nullptr, info_log);
        std::cerr << "[AudioVisualizer] 片段着色器编译失败: " << info_log << std::endl;
        glDeleteShader(vs);
        return false;
    }

    shader_program_ = glCreateProgram();
    glAttachShader(shader_program_, vs);
    glAttachShader(shader_program_, fs);
    glLinkProgram(shader_program_);

    glGetProgramiv(shader_program_, GL_LINK_STATUS, &success);
    if (!success) {
        char info_log[512];
        glGetProgramInfoLog(shader_program_, 512, nullptr, info_log);
        std::cerr << "[AudioVisualizer] 着色器链接失败: " << info_log << std::endl;
        glDeleteShader(vs);
        glDeleteShader(fs);
        return false;
    }

    glDeleteShader(vs);
    glDeleteShader(fs);

    // --------------------------------------------------------
    // 创建VAO/VBO（动态更新，每帧重新上传顶点数据）
    // --------------------------------------------------------
    glGenVertexArrays(1, &vao_);
    glGenBuffers(1, &vbo_);

    // 每个柱状图 = 2个三角形 = 6个顶点
    // 每个顶点 = 2个位置分量 + 3个颜色分量 = 5个float
    // 总共 = bar_count * 6 * 5 个float
    glBindVertexArray(vao_);
    glBindBuffer(GL_ARRAY_BUFFER, vbo_);
    glBufferData(GL_ARRAY_BUFFER, bar_count_ * 6 * 5 * sizeof(float), nullptr, GL_DYNAMIC_DRAW);

    // 位置属性
    glVertexAttribPointer(0, 2, GL_FLOAT, GL_FALSE, 5 * sizeof(float), (void*)0);
    glEnableVertexAttribArray(0);

    // 颜色属性
    glVertexAttribPointer(1, 3, GL_FLOAT, GL_FALSE, 5 * sizeof(float), (void*)(2 * sizeof(float)));
    glEnableVertexAttribArray(1);

    glBindBuffer(GL_ARRAY_BUFFER, 0);
    glBindVertexArray(0);

    std::cout << "[AudioVisualizer] 初始化成功: " << bar_count_ << " 频谱柱" << std::endl;
    return true;
}

void AudioVisualizer::cleanup()
{
    if (vao_) { glDeleteVertexArrays(1, &vao_); vao_ = 0; }
    if (vbo_) { glDeleteBuffers(1, &vbo_); vbo_ = 0; }
    if (shader_program_) { glDeleteProgram(shader_program_); shader_program_ = 0; }
}

void AudioVisualizer::setSpectrumData(const std::vector<float>& spectrum_data)
{
    int count = std::min(bar_count_, (int)spectrum_data.size());
    for (int i = 0; i < count; i++) {
        float value = std::max(0.0f, std::min(1.0f, spectrum_data[i]));
        spectrum_data_[i] = value;
        smoothed_data_[i] = 0.3f * value + 0.7f * smoothed_data_[i];
    }
    for (int i = count; i < bar_count_; i++) {
        spectrum_data_[i] = 0.0f;
        smoothed_data_[i] *= 0.7f;
    }
}

// ============================================================
// FFT实现 - Cooley-Tukey radix-2 算法
// ============================================================
void AudioVisualizer::fft(std::vector<std::complex<double>>& data)
{
    int N = (int)data.size();

    // --------------------------------------------------------
    // 位反转排列(Bit-Reversal Permutation)
    // 
    /** FFT的递归展开后，需要将输入数据按照位反转顺序重排
    * 例如：N=8时，索引3(011)与索引6(110)交换
    * 这是Cooley-Tukey算法的in-place实现的关键步骤*/
    // --------------------------------------------------------
    for (int i = 1, j = 0; i < N; i++) {
        int bit = N >> 1;
        for (; j & bit; bit >>= 1) {
            j ^= bit;
        }
        j ^= bit;
        if (i < j) {
            std::swap(data[i], data[j]);
        }
    }

    // --------------------------------------------------------
   /* * 迭代FFT计算
    * 
    * 从长度2的DFT开始，逐步合并为长度4、8、...、N的DFT
    * 每个阶段使用旋转因子(twiddle factor)合并
    * 
    * 旋转因子：W_N^k = e^(-2πik/N)
    * 利用对称性：W_N^(k+N/2) = -W_N^k
    * 可以一次计算一对蝶形运算(butterfly operation)*/
    // --------------------------------------------------------
    for (int len = 2; len <= N; len <<= 1) {
        double angle = -2.0 * M_PI / len;
        std::complex<double> wlen(cos(angle), sin(angle));

        for (int i = 0; i < N; i += len) {
            std::complex<double> w(1.0, 0.0);
            for (int j = 0; j < len / 2; j++) {
                // 蝶形运算
                std::complex<double> u = data[i + j];
                std::complex<double> v = w * data[i + j + len / 2];
                data[i + j] = u + v;
                data[i + j + len / 2] = u - v;
                w *= wlen;
            }
        }
    }
}

// ============================================================
// 处理音频数据
// ============================================================
void AudioVisualizer::processAudioData(const uint8_t* pcm_data, int data_size, int channels, int sample_rate)
{
    // 将16位PCM数据转换为复数数组（用于FFT输入）
    // 只取左声道数据（如果是立体声）
    int samples = data_size / (channels * 2);  // 16位=2字节
    int fft_samples = std::min(samples, FFT_SIZE);

    // 填充FFT输入缓冲区
    // 1. 取最近的fft_samples个采样（模拟滑动窗口）
    // 2. 乘以窗函数（减少频谱泄漏）
    // 3. 不足FFT_SIZE的部分补零(zero-padding)
    for (int i = 0; i < FFT_SIZE; i++) {
        if (i < fft_samples) {
            int idx = (samples - fft_samples + i) * channels;
            int16_t sample = (int16_t)(pcm_data[idx * 2 + 1] << 8 | pcm_data[idx * 2]);
            // 归一化到[-1, 1]范围并乘窗函数
            fft_input_[i] = std::complex<double>(sample / 32768.0 * window_[i], 0.0);
        }
        else {
            fft_input_[i] = std::complex<double>(0.0, 0.0);
        }
    }

    // 执行FFT
    fft(fft_input_);

    // 计算频谱数据
    computeSpectrum();
}

// ============================================================
// 计算频谱柱状图数据
// ============================================================
void AudioVisualizer::computeSpectrum()
{
    // FFT输出是对称的（实信号的FFT性质），只取前半部分
    int usable_bins = FFT_SIZE / 2;

    // 将频率bin分组到bar_count_个柱状图中
    // 使用对数缩放：低频段更精细，高频段更粗略
    // 这符合人耳对频率的感知（对数尺度）
    int bins_per_bar = usable_bins / bar_count_;

    for (int i = 0; i < bar_count_; i++) {
        double magnitude_sum = 0.0;
        int start_bin = i * bins_per_bar;
        int end_bin = start_bin + bins_per_bar;

        for (int j = start_bin; j < end_bin && j < usable_bins; j++) {
            // 计算幅度(magnitude)：|X[k]| = sqrt(Re² + Im²)
            double magnitude = std::abs(fft_input_[j]);
            magnitude_sum += magnitude;
        }

        // 取平均值并归一化
        double avg_magnitude = magnitude_sum / bins_per_bar;

        // 转换为分贝(dB)尺度，然后归一化到[0,1]
        // dB = 20 * log10(magnitude)
        // 使用dB尺度更符合人耳对响度的感知
        double db = 20.0 * log10(avg_magnitude + 1e-10);
        // 将dB范围[-60, 0]映射到[0, 1]
        float normalized = std::max(0.0f, std::min(1.0f, (float)((db + 60.0) / 60.0)));

        spectrum_data_[i] = normalized;

        // 平滑处理：指数移动平均
        // smooth = α * new + (1-α) * old
        // α越小，变化越平滑（但延迟越大）
        float alpha = 0.3f;
        smoothed_data_[i] = alpha * normalized + (1.0f - alpha) * smoothed_data_[i];
    }
}

// ============================================================
// 渲染频谱图
// ============================================================
void AudioVisualizer::render(int width, int height)
{
    if (!shader_program_ || !vao_) return;

    glViewport(0, 0, width, height);

    // 构建顶点数据：每个柱状图由2个三角形组成
    std::vector<float> vertices;
    vertices.reserve(bar_count_ * 6 * 5);  // bar * 6顶点 * 5分量

    float bar_width = 2.0f / bar_count_;  // NDC空间中每个柱的宽度
    float gap = bar_width * 0.1f;         // 柱间间隔

    for (int i = 0; i < bar_count_; i++) {
        float x1 = -1.0f + i * bar_width + gap / 2;
        float x2 = -1.0f + (i + 1) * bar_width - gap / 2;
        float bar_height = smoothed_data_[i] * 1.8f;  // 最大高度映射到0.9（NDC范围-1~1）
        float y1 = -1.0f;
        float y2 = -1.0f + bar_height;

        // 颜色渐变：低频=蓝色 → 中频=绿色 → 高频=红色
        float t = (float)i / bar_count_;
        float r, g, b;
        if (t < 0.33f) {
            // 蓝→青
            r = 0.0f;
            g = t * 3.0f;
            b = 1.0f;
        }
        else if (t < 0.66f) {
            // 青→绿→黄
            r = (t - 0.33f) * 3.0f;
            g = 1.0f;
            b = 1.0f - (t - 0.33f) * 3.0f;
        }
        else {
            // 黄→红
            r = 1.0f;
            g = 1.0f - (t - 0.66f) * 3.0f;
            b = 0.0f;
        }

        // 三角形1：左下 → 右下 → 右上
        vertices.insert(vertices.end(), { x1, y1, r, g, b });
        vertices.insert(vertices.end(), { x2, y1, r, g, b });
        vertices.insert(vertices.end(), { x2, y2, r, g, b });

        // 三角形2：左下 → 右上 → 左上
        vertices.insert(vertices.end(), { x1, y1, r, g, b });
        vertices.insert(vertices.end(), { x2, y2, r, g, b });
        vertices.insert(vertices.end(), { x1, y2, r, g, b });
    }

    // 上传顶点数据到GPU
    glBindBuffer(GL_ARRAY_BUFFER, vbo_);
    glBufferSubData(GL_ARRAY_BUFFER, 0, vertices.size() * sizeof(float), vertices.data());
    glBindBuffer(GL_ARRAY_BUFFER, 0);

    // 渲染
    glUseProgram(shader_program_);
    glBindVertexArray(vao_);
    glDrawArrays(GL_TRIANGLES, 0, bar_count_ * 6);
    glBindVertexArray(0);
    glUseProgram(0);
}
