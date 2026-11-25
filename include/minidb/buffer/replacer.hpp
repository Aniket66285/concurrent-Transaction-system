
#pragma once

#include "minidb/common/types.hpp"

#include <cstddef>

namespace minidb {

class Replacer {
public:
    virtual ~Replacer() = default;

    // Select and remove the least recently used unpinned frame.
    virtual bool Victim(frame_id_t* frame_id) = 0;

    // Remove a frame from the replacement candidates.
    virtual void Pin(frame_id_t frame_id) = 0;

    // Add an unpinned frame as a replacement candidate.
    virtual void Unpin(frame_id_t frame_id) = 0;

    // Return the number of available replacement candidates.
    virtual std::size_t Size() const = 0;
};

} // namespace minidb
