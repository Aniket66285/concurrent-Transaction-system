
#pragma once

#include "minidb/buffer/replacer.hpp"
#include "minidb/common/config.hpp"

#include <cstddef>
#include <list>
#include <mutex>
#include <unordered_map>

namespace minidb {

class LRUReplacer : public Replacer {
public:
    explicit LRUReplacer(
        std::size_t num_frames = Config::kBufferPoolFrames
    );

    ~LRUReplacer() override = default;

    LRUReplacer(const LRUReplacer&) = delete;
    LRUReplacer& operator=(const LRUReplacer&) = delete;

    bool Victim(frame_id_t* frame_id) override;

    void Pin(frame_id_t frame_id) override;

    void Unpin(frame_id_t frame_id) override;

    std::size_t Size() const override;

    bool Contains(frame_id_t frame_id) const;

private:
    using LRUList = std::list<frame_id_t>;
    using LRUIterator = LRUList::iterator;

    void ValidateFrameId(frame_id_t frame_id) const;

    const std::size_t capacity_;

    // Front = Least Recently Used.
    // Back = Most Recently Used.
    LRUList lru_list_;

    // Quickly finds the position of a frame in the LRU list.
    std::unordered_map<frame_id_t, LRUIterator> positions_;

    mutable std::mutex mutex_;
};

} // namespace minidb
