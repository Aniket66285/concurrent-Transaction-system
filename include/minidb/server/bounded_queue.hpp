#pragma once

#include "minidb/server/semaphore_gate.hpp"

#include <cstddef>
#include <limits>
#include <mutex>
#include <optional>
#include <queue>
#include <stdexcept>
#include <utility>

namespace minidb {

template <typename T>
class BoundedQueue {
public:
    explicit BoundedQueue(std::size_t capacity)
        : capacity_(capacity),
          available_slots_(ValidateCapacity(capacity)),
          available_items_(0) {}

    BoundedQueue(const BoundedQueue&) = delete;
    BoundedQueue& operator=(const BoundedQueue&) = delete;

    void Push(T item) {
        available_slots_.Wait();
        try {
            std::lock_guard<std::mutex> lock(queue_mutex_);
            queue_.push(std::move(item));
        } catch (...) {
            available_slots_.Post();
            throw;
        }
        available_items_.Post();
    }

    T Pop() {
        available_items_.Wait();
        std::optional<T> item;
        try {
            std::lock_guard<std::mutex> lock(queue_mutex_);
            item.emplace(std::move(queue_.front()));
            queue_.pop();
        } catch (...) {
            available_items_.Post();
            throw;
        }
        available_slots_.Post();
        return std::move(*item);
    }

    std::size_t GetSize() const {
        std::lock_guard<std::mutex> lock(queue_mutex_);
        return queue_.size();
    }

    std::size_t GetCapacity() const noexcept { return capacity_; }

private:
    static unsigned int ValidateCapacity(std::size_t capacity) {
        if (capacity == 0 ||
            capacity > std::numeric_limits<unsigned int>::max()) {
            throw std::invalid_argument("Queue capacity must be a positive semaphore-compatible value");
        }
        return static_cast<unsigned int>(capacity);
    }

    const std::size_t capacity_;
    SemaphoreGate available_slots_;
    SemaphoreGate available_items_;
    mutable std::mutex queue_mutex_;
    std::queue<T> queue_;
};

} // namespace minidb
