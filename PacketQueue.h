#pragma once

#include <condition_variable>
#include <cstdint>
#include <mutex>
#include <queue>

extern "C" {
#include <libavcodec/avcodec.h>
}

class PacketQueue {
public:
    PacketQueue();
    ~PacketQueue();

    PacketQueue(const PacketQueue&) = delete;
    PacketQueue& operator=(const PacketQueue&) = delete;

    bool push(const AVPacket* packet);
    bool pop(AVPacket* packet, bool block);
    void clear();
    void abort();
    void reset();
    void finish();

    int size() const;
    int64_t duration() const;
    bool empty() const;
    bool isAborted() const;
    bool isFinished() const;

private:
    void clearLocked();

    mutable std::mutex mutex_;
    std::condition_variable condition_;
    std::queue<AVPacket*> packets_;
    int size_;
    int64_t duration_;
    bool abort_requested_;
    bool finished_;
};
