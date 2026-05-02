/**
 * @file AudioVisualizer.h
 * @brief 音频可视化器 - FFT频谱分析与OpenGL渲染
 * 
 * 本类实现了实时音频频谱可视化，核心流程：
 * 
 * 1. 音频数据采集：从PCM数据中获取时域信号
 * 2. FFT变换：将时域信号转换为频域信号
 * 3. 频谱渲染：使用OpenGL绘制频谱柱状图
 * 
 * FFT (Fast Fourier Transform) 快速傅里叶变换：
 * - 将时域信号（振幅随时间变化）转换为频域信号（振幅随频率变化）
 * - 算法复杂度：O(N log N)，比朴素DFT的O(N²)快得多
 * - Cooley-Tukey算法：最常用的FFT实现，采用分治策略
 * 
 * 频谱可视化原理：
 * - 横轴：频率（低频→高频，左→右）
 * - 纵轴：振幅（该频率分量的能量大小）
 * - 柱状图高度反映各频段的能量
 * 
 * 学习要点：
 * - FFT算法实现（Cooley-Tukey radix-2）
 * - 复数运算（欧拉公式：e^(ix) = cos(x) + i*sin(x)）
 * - OpenGL动态几何体渲染
 * - 音频频谱分析基础
 */

#pragma once

#include <GL/glew.h>
#include <vector>
#include <complex>
#include <string>

class AudioVisualizer {
public:
    AudioVisualizer();
    ~AudioVisualizer();

    /**
     * @brief 初始化可视化器
     * @param bar_count 频谱柱状图数量（默认64）
     * @return true=成功
     */
    bool init(int bar_count = 64);

    /** @brief 释放资源 */
    void cleanup();

    /**
     * @brief 输入音频PCM数据并执行FFT
     * @param pcm_data PCM音频数据（16位有符号整数）
     * @param data_size 数据大小（字节）
     * @param channels 声道数
     * @param sample_rate 采样率
     */
    void processAudioData(const uint8_t* pcm_data, int data_size, int channels, int sample_rate);

    /**
     * @brief 渲染频谱图
     * @param width 渲染区域宽度
     * @param height 渲染区域高度
     */
    void render(int width, int height);

    /** @brief 获取频谱数据（用于调试/显示） */
    const std::vector<float>& getSpectrumData() const { return spectrum_data_; }

private:
    /**
     * @brief Cooley-Tukey FFT算法实现
     * @param data 复数数组（输入时域信号，输出频域信号）
     * 
     * 算法原理：
     * 1. 如果N=1，直接返回（递归基）
     * 2. 将N点DFT分解为偶数索引和奇数索引的N/2点DFT
     * 3. 递归计算两个N/2点DFT
     * 4. 使用旋转因子(twiddle factor)合并结果
     * 
     * 旋转因子：W_N^k = e^(-2πik/N) = cos(2πk/N) - i*sin(2πk/N)
     */
    void fft(std::vector<std::complex<double>>& data);

    /** @brief 将PCM数据转换为频谱柱状图数据 */
    void computeSpectrum();

    // OpenGL资源
    GLuint shader_program_;     // 着色器程序
    GLuint vao_;                // 顶点数组对象
    GLuint vbo_;                // 顶点缓冲对象

    // 频谱参数
    int bar_count_;             // 柱状图数量
    std::vector<float> spectrum_data_;   // 频谱数据（归一化振幅，0~1）
    std::vector<float> smoothed_data_;   // 平滑后的频谱数据（视觉更柔和）

    // FFT缓冲区
    std::vector<std::complex<double>> fft_input_;  // FFT输入缓冲
    std::vector<double> window_;                    // 窗函数系数

    static constexpr int FFT_SIZE = 2048;  // FFT窗口大小（必须是2的幂）
};
