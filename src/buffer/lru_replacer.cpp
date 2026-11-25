
#include "minidb/buffer/lru_replacer.hpp"

#include "minidb/common/exception.hpp"

#include <iterator>
#include <string>

namespace minidb {

LRUReplacer::LRUReplacer(std::size_t num_frames)
    : capacity_(num_frames) {

    if (capacity_ == 0) {
        throw StorageException(
            "LRUReplacer capacity cannot be zero"
        );
    }
}

void LRUReplacer::ValidateFrameId(frame_id_t frame_id) const {

    if (static_cast<std::size_t>(frame_id) >= capacity_) {
        throw StorageException(
            "LRUReplacer received an invalid frame ID: " +
            std::to_string(frame_id)
        );
    }
}

bool LRUReplacer::Victim(frame_id_t* frame_id) {

    if (frame_id == nullptr) {
        return false;
    }

    std::lock_guard<std::mutex> lock(mutex_);

    if (lru_list_.empty()) {
        return false;
    }

    // Front contains the least recently used frame.
    const frame_id_t victim_frame = lru_list_.front();

    // Remove it from both data structures.
    lru_list_.pop_front();
    positions_.erase(victim_frame);

    *frame_id = victim_frame;

    return true;
}

void LRUReplacer::Pin(frame_id_t frame_id) {

    ValidateFrameId(frame_id);

    std::lock_guard<std::mutex> lock(mutex_);

    auto it = positions_.find(frame_id);

    // If the frame is currently an eviction candidate,
    // remove it because it is now pinned.
    if (it != positions_.end()) {

        lru_list_.erase(it->second);

        positions_.erase(it);
    }
}

void LRUReplacer::Unpin(frame_id_t frame_id) {

    ValidateFrameId(frame_id);

    std::lock_guard<std::mutex> lock(mutex_);

    // Avoid duplicate entries.
    // Repeated Unpin must not change the existing LRU order.
    if (positions_.find(frame_id) != positions_.end()) {
        return;
    }

    // Newly unpinned frame becomes the most recently used
    // replacement candidate.
    auto list_it = lru_list_.insert(
        lru_list_.end(),
        frame_id
    );

    try {

        positions_.emplace(frame_id, list_it);

    } catch (...) {

        lru_list_.erase(list_it);

        throw;
    }
}

std::size_t LRUReplacer::Size() const {

    std::lock_guard<std::mutex> lock(mutex_);

    return lru_list_.size();
}

bool LRUReplacer::Contains(frame_id_t frame_id) const {

    ValidateFrameId(frame_id);

    std::lock_guard<std::mutex> lock(mutex_);

    return positions_.find(frame_id) != positions_.end();
}

} // namespace minidb
