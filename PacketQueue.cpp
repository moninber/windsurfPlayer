#include "PacketQueue.h"

PacketQueue::PacketQueue()
    : size_(0)
    , duration_(0)
    , abort_requested_(false)
    , finished_(false)
{
}

PacketQueue::~PacketQueue()
{
    clear();
}

bool PacketQueue::push(const AVPacket* packet)
{
    if (!packet) return false;

    AVPacket* copy = av_packet_alloc();
    if (!copy) return false;

    if (av_packet_ref(copy, packet) < 0) {
        av_packet_free(&copy);
        return false;
    }

    std::lock_guard<std::mutex> lock(mutex_);
    if (abort_requested_ || finished_) {
        av_packet_free(&copy);
        return false;
    }

    packets_.push(copy);
    ++size_;
    if (copy->duration > 0) {
        duration_ += copy->duration;
    }
    condition_.notify_one();
    return true;
}

bool PacketQueue::pop(AVPacket* packet, bool block)
{
    if (!packet) return false;

    std::unique_lock<std::mutex> lock(mutex_);
    while (packets_.empty() && !abort_requested_ && !finished_) {
        if (!block) return false;
        condition_.wait(lock);
    }

    if (abort_requested_ || packets_.empty()) return false;

    AVPacket* front = packets_.front();
    packets_.pop();
    --size_;
    if (front->duration > 0) {
        duration_ -= front->duration;
    }

    av_packet_move_ref(packet, front);
    av_packet_free(&front);
    return true;
}

void PacketQueue::clear()
{
    std::lock_guard<std::mutex> lock(mutex_);
    clearLocked();
}

void PacketQueue::abort()
{
    {
        std::lock_guard<std::mutex> lock(mutex_);
        abort_requested_ = true;
        finished_ = true;
        clearLocked();
    }
    condition_.notify_all();
}

void PacketQueue::reset()
{
    {
        std::lock_guard<std::mutex> lock(mutex_);
        abort_requested_ = false;
        finished_ = false;
        clearLocked();
    }
    condition_.notify_all();
}

void PacketQueue::finish()
{
    {
        std::lock_guard<std::mutex> lock(mutex_);
        finished_ = true;
    }
    condition_.notify_all();
}

int PacketQueue::size() const
{
    std::lock_guard<std::mutex> lock(mutex_);
    return size_;
}

int64_t PacketQueue::duration() const
{
    std::lock_guard<std::mutex> lock(mutex_);
    return duration_;
}

bool PacketQueue::empty() const
{
    std::lock_guard<std::mutex> lock(mutex_);
    return packets_.empty();
}

bool PacketQueue::isAborted() const
{
    std::lock_guard<std::mutex> lock(mutex_);
    return abort_requested_;
}

bool PacketQueue::isFinished() const
{
    std::lock_guard<std::mutex> lock(mutex_);
    return finished_;
}

void PacketQueue::clearLocked()
{
    while (!packets_.empty()) {
        AVPacket* packet = packets_.front();
        packets_.pop();
        av_packet_free(&packet);
    }
    size_ = 0;
    duration_ = 0;
}
