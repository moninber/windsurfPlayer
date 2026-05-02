/**
 * @file FrameQueue.h
 * @brief 线程安全帧队列 - 生产者-消费者模式实现
 * 
 * 本文件实现了一个线程安全的模板队列，用于在解码线程（生产者）
 * 和渲染线程（消费者）之间传递音视频帧数据。
 * 
 * 核心设计模式：生产者-消费者模式（Producer-Consumer Pattern）
 * - 生产者：解码线程将解码后的帧数据推入队列
 * - 消费者：渲染线程从队列中取出帧数据进行渲染
 * - 同步机制：使用 mutex + condition_variable 实现线程同步
 * - 背压控制：队列容量上限防止内存溢出
 * 
 * 为什么需要帧队列？
 * 1. 解码和渲染的速度不同，需要缓冲区来平滑速度差异
 * 2. 解码线程可以提前解码若干帧，减少渲染时的等待时间
 * 3. 线程解耦：解码线程不需要等待渲染完成
 * 
 * 学习要点：
 * - std::mutex：互斥锁，保护共享数据的原子访问
 * - std::condition_variable：条件变量，实现线程间的等待/通知机制
 * - std::unique_lock：锁管理器，可与condition_variable配合使用
 * - move语义：避免帧数据的深拷贝，提升性能
 */

#pragma once

#include <queue>
#include <mutex>
#include <condition_variable>
#include <atomic>
#include <chrono>

template<typename T>
class FrameQueue {
public:
    /**
     * @brief 构造函数
     * @param max_size 队列最大容量（默认30，约1秒的缓冲@30fps）
     */
    explicit FrameQueue(size_t max_size = 30)
        : max_size_(max_size)
        , finished_(false)
    {
    }

    /**
     * @brief 生产者：向队列推入一帧数据
     * @param frame 帧数据（使用右值引用，支持move语义避免拷贝）
     * @return true=推入成功, false=队列已关闭
     * 
     * 工作流程：
     * 1. 获取互斥锁
     * 2. 如果队列已满，等待消费者取走帧（条件变量等待）
     * 3. 将帧数据move到队列中（避免深拷贝）
     * 4. 通知等待的消费者有新数据可用
     */
    bool push(T&& frame) {
        std::unique_lock<std::mutex> lock(mutex_);
        
        // 等待队列有空间：当队列未满 或 队列已标记完成时退出等待
        not_full_.wait(lock, [this]() {
            return queue_.size() < max_size_ || finished_;
        });
        
        // 如果队列已标记完成，拒绝新数据
        if (finished_) {
            return false;
        }
        
        // 使用std::move将帧数据移入队列（零拷贝）
        queue_.push(std::move(frame));
        
        // 通知可能正在等待的消费者：队列中有新数据了
        not_empty_.notify_one();
        return true;
    }

    /**
     * @brief 消费者：从队列取出一帧数据
     * @param frame 输出参数，接收取出的帧数据
     * @param timeout_ms 超时时间（毫秒），0=无限等待
     * @return true=取出成功, false=队列为空且已标记完成
     * 
     * 工作流程：
     * 1. 获取互斥锁
     * 2. 如果队列为空，等待生产者推入新帧
     * 3. 从队列头部取出帧数据（move语义）
     * 4. 通知等待的生产者：队列有空间了
     */
    bool pop(T& frame, int timeout_ms = 0) {
        std::unique_lock<std::mutex> lock(mutex_);
        
        if (timeout_ms > 0) {
            // 带超时的等待，避免无限阻塞
            bool has_data = not_empty_.wait_for(lock,
                std::chrono::milliseconds(timeout_ms),
                [this]() { return !queue_.empty() || finished_; });
            
            if (!has_data && queue_.empty()) {
                return false;
            }
        } else {
            // 无限等待，直到有数据或队列结束
            not_empty_.wait(lock, [this]() {
                return !queue_.empty() || finished_;
            });
        }
        
        // 队列为空且已标记完成 → 所有数据已消费完毕
        if (queue_.empty()) {
            return false;
        }
        
        // 从队列头部取出帧（move避免拷贝）
        frame = std::move(queue_.front());
        queue_.pop();
        
        // 通知可能正在等待的生产者：队列有空间了
        not_full_.notify_one();
        return true;
    }

    /**
     * @brief 标记队列完成（生产者不再生产新数据）
     * 
     * 当解码线程到达文件末尾时调用，通知消费者：
     * "不会再有新数据了，取完剩余数据后可以退出"
     */
    void finish() {
        std::lock_guard<std::mutex> lock(mutex_);
        finished_ = true;
        // 唤醒所有等待的线程，让它们检查finished_标志
        not_empty_.notify_all();
        not_full_.notify_all();
    }

    /** @brief 重置队列状态，允许重新使用 */
    void reset() {
        std::lock_guard<std::mutex> lock(mutex_);
        // 清空队列中的剩余数据
        while (!queue_.empty()) {
            queue_.pop();
        }
        finished_ = false;
    }

    /** @brief 获取当前队列中的帧数 */
    size_t size() const {
        std::lock_guard<std::mutex> lock(mutex_);
        return queue_.size();
    }

    /** @brief 队列是否为空 */
    bool empty() const {
        std::lock_guard<std::mutex> lock(mutex_);
        return queue_.empty();
    }

    /** @brief 队列是否已标记完成 */
    bool isFinished() const {
        return finished_.load();
    }

private:
    std::queue<T> queue_;                      // 底层队列容器
    mutable std::mutex mutex_;                 // 互斥锁（mutable允许const函数加锁）
    std::condition_variable not_empty_;        // 条件变量：队列非空通知
    std::condition_variable not_full_;         // 条件变量：队列未满通知
    size_t max_size_;                          // 队列最大容量
    std::atomic<bool> finished_;               // 队列完成标志（原子变量）
};
