#pragma once

#include <deque>
#include <mutex>
#include <condition_variable>
#include <atomic>

// Simple thread-safe bounded ring buffer with optional drop-oldest policy.
template <typename T>
class RingBuffer {
public:
    RingBuffer(size_t capacity = 8, bool drop_oldest = true)
        : capacity_(capacity), drop_oldest_(drop_oldest), closed_(false) {}

    // Push an item. If buffer is full:
    // - If drop_oldest_ is true, the oldest item is removed and the push succeeds.
    // - Otherwise, the push fails and returns false.
    bool push(const T& item) {
        std::unique_lock<std::mutex> lk(m_);
        if (closed_) return false;
        if (buffer_.size() >= capacity_) {
            if (drop_oldest_) {
                buffer_.pop_front();
            } else {
                return false;
            }
        }
        buffer_.push_back(item);
        lk.unlock();
        cv_.notify_one();
        return true;
    }

    // Blocking pop. Returns false if buffer is closed and empty.
    bool pop(T& out) {
        std::unique_lock<std::mutex> lk(m_);
        cv_.wait(lk, [&]{ return closed_ || !buffer_.empty(); });
        if (buffer_.empty()) return false; // closed and empty
        out = std::move(buffer_.front());
        buffer_.pop_front();
        return true;
    }

    void close() {
        std::unique_lock<std::mutex> lk(m_);
        closed_ = true;
        lk.unlock();
        cv_.notify_all();
    }

    size_t size() const {
        std::unique_lock<std::mutex> lk(m_);
        return buffer_.size();
    }

private:
    size_t capacity_;
    bool drop_oldest_;
    mutable std::mutex m_;
    std::condition_variable cv_;
    std::deque<T> buffer_;
    std::atomic<bool> closed_;
};
