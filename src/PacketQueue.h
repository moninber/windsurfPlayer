#pragma once

#include <condition_variable>
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
    void abort();
    void reset();
    void finish();

    int size() const;
    bool isAborted() const;
    bool isFinished() const;

private:
    void clearLocked();

    mutable std::mutex mutex_;
    std::condition_variable condition_;
    std::queue<AVPacket*> packets_;
    int size_;
    bool abort_requested_;
    bool finished_;
};
