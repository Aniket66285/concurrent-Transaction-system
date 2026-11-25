
#include "minidb/buffer/buffer_pool.hpp"

#include "minidb/common/exception.hpp"

#include <limits>
#include <string>
#include <utility>

namespace minidb {

std::size_t BufferPool::ValidatePoolSize(std::size_t pool_size) {

    if (pool_size == 0) {
        throw StorageException(
            "BufferPool size cannot be zero"
        );
    }

    if (pool_size >
        static_cast<std::size_t>(
            std::numeric_limits<frame_id_t>::max()
        )) {

        throw StorageException(
            "BufferPool size exceeds frame ID capacity"
        );
    }

    return pool_size;
}

BufferPool::BufferPool(
    DiskManager& disk_manager,
    std::size_t pool_size,
    WalFlushCallback wal_flush_callback
)
    : disk_manager_(disk_manager),
      pool_size_(ValidatePoolSize(pool_size)),
      frames_(std::make_unique<Frame[]>(pool_size_)),
      replacer_(pool_size_),
      wal_flush_callback_(std::move(wal_flush_callback)) {

    // Reverse order because pop_back() should return frame 0 first.
    for (std::size_t i = pool_size_; i > 0; --i) {
        free_list_.push_back(
            static_cast<frame_id_t>(i - 1)
        );
    }
}

BufferPool::~BufferPool() noexcept {

    // Best-effort shutdown flush.
    // Explicit FlushAllPages() should be used when flush errors
    // need to be reported to the caller.
    try {
        FlushAllPages();
    }
    catch (...) {
        // Destructors must not throw.
    }
}

void BufferPool::ResetFrameLocked(frame_id_t frame_id) {

    Frame& frame = frames_[frame_id];

    frame.data.fill(0);
    frame.page_id = 0;
    frame.pin_count = 0;
    frame.is_dirty = false;
    frame.valid = false;
}

void BufferPool::PinFrameLocked(frame_id_t frame_id) {

    Frame& frame = frames_[frame_id];

    if (!frame.valid) {
        throw StorageException(
            "Cannot pin an invalid buffer frame"
        );
    }

    // A frame with pin count zero is currently an LRU candidate.
    if (frame.pin_count == 0) {
        replacer_.Pin(frame_id);
    }

    ++frame.pin_count;
}

void BufferPool::WriteFrameLocked(frame_id_t frame_id) {

    Frame& frame = frames_[frame_id];

    if (!frame.valid || !frame.is_dirty) {
        return;
    }

    // WAL-before-data ordering.
    // Flush the WAL before writing this dirty page.
    if (wal_flush_callback_) {
        wal_flush_callback_();
    }

    disk_manager_.WritePage(frame.page_id, frame.data);
}

bool BufferPool::AcquireFrameLocked(frame_id_t& frame_id) {

    // First preference: an unused frame.
    if (!free_list_.empty()) {

        frame_id = free_list_.back();
        free_list_.pop_back();

        return true;
    }

    // No free frames. Ask the LRU replacer for a victim.
    if (!replacer_.Victim(&frame_id)) {
        return false;
    }

    Frame& victim = frames_[frame_id];

    if (!victim.valid || victim.pin_count != 0) {

        // Restore the candidate before reporting an invariant error.
        try {
            replacer_.Unpin(frame_id);
        }
        catch (...) {
        }

        throw StorageException(
            "LRUReplacer returned an invalid or pinned frame"
        );
    }

    // Dirty victim must be written before its frame is reused.
    try {
        WriteFrameLocked(frame_id);
        victim.is_dirty = false;
    }
    catch (...) {

        // Do not lose the old page if its write fails.
        try {
            replacer_.Unpin(frame_id);
        }
        catch (...) {
        }

        throw;
    }

    const auto page_it = page_table_.find(victim.page_id);

    if (page_it == page_table_.end() ||
        page_it->second != frame_id) {

        throw StorageException(
            "BufferPool page table is inconsistent during eviction"
        );
    }

    page_table_.erase(page_it);

    ResetFrameLocked(frame_id);

    return true;
}

PageGuard BufferPool::NewPage(page_id_t* page_id) {

    frame_id_t frame_id = 0;
    page_id_t new_page_id = 0;

    {
        std::lock_guard<std::mutex> lock(mutex_);

        if (!AcquireFrameLocked(frame_id)) {
            return {};
        }

        try {

            new_page_id = disk_manager_.AllocatePage();

            Frame& frame = frames_[frame_id];

            frame.data.fill(0);
            frame.page_id = new_page_id;
            frame.pin_count = 1;
            frame.is_dirty = false;
            frame.valid = true;

            const auto result = page_table_.emplace(
                new_page_id,
                frame_id
            );

            if (!result.second) {
                throw StorageException(
                    "Duplicate page ID during NewPage"
                );
            }
        }
        catch (...) {

            ResetFrameLocked(frame_id);
            free_list_.push_back(frame_id);

            throw;
        }
    }

    if (page_id != nullptr) {
        *page_id = new_page_id;
    }

    try {

        Frame& frame = frames_[frame_id];

        return PageGuard(
            this,
            frame_id,
            new_page_id,
            &frame.data,
            frame.latch
        );
    }
    catch (...) {

        UnpinFrame(frame_id, new_page_id, false);

        throw;
    }
}

PageGuard BufferPool::FetchPage(page_id_t page_id) {

    frame_id_t frame_id = 0;

    {
        std::lock_guard<std::mutex> lock(mutex_);

        const auto existing = page_table_.find(page_id);

        if (existing != page_table_.end()) {

            frame_id = existing->second;

            PinFrameLocked(frame_id);
        }
        else {

            // Read into temporary storage first.
            // A failed read must not evict a valid resident page.
            DiskManager::PageData loaded_page{};

            disk_manager_.ReadPage(
                page_id,
                loaded_page
            );

            if (!AcquireFrameLocked(frame_id)) {
                return {};
            }

            Frame& frame = frames_[frame_id];

            frame.data = loaded_page;
            frame.page_id = page_id;
            frame.pin_count = 1;
            frame.is_dirty = false;
            frame.valid = true;

            try {

                const auto result = page_table_.emplace(
                    page_id,
                    frame_id
                );

                if (!result.second) {
                    throw StorageException(
                        "Duplicate page ID during FetchPage"
                    );
                }
            }
            catch (...) {

                ResetFrameLocked(frame_id);
                free_list_.push_back(frame_id);

                throw;
            }
        }
    }

    try {

        Frame& frame = frames_[frame_id];

        return PageGuard(
            this,
            frame_id,
            page_id,
            &frame.data,
            frame.latch
        );
    }
    catch (...) {

        UnpinFrame(frame_id, page_id, false);

        throw;
    }
}

void BufferPool::UnpinFrame(
    frame_id_t frame_id,
    page_id_t page_id,
    bool is_dirty
) noexcept {

    try {

        std::lock_guard<std::mutex> lock(mutex_);

        if (static_cast<std::size_t>(frame_id) >= pool_size_) {
            return;
        }

        Frame& frame = frames_[frame_id];

        if (!frame.valid ||
            frame.page_id != page_id ||
            frame.pin_count == 0) {

            return;
        }

        if (is_dirty) {
            frame.is_dirty = true;
        }

        --frame.pin_count;

        if (frame.pin_count == 0) {
            replacer_.Unpin(frame_id);
        }
    }
    catch (...) {
        // Unpin is called by PageGuard destruction and must not throw.
    }
}

bool BufferPool::FlushPage(page_id_t page_id) {

    std::lock_guard<std::mutex> lock(mutex_);

    const auto it = page_table_.find(page_id);

    if (it == page_table_.end()) {
        return false;
    }

    const frame_id_t frame_id = it->second;

    Frame& frame = frames_[frame_id];

    // Wait until active page guards release their page latch.
    std::unique_lock<std::shared_mutex> page_lock(frame.latch);

    if (frame.is_dirty) {

        WriteFrameLocked(frame_id);

        // Explicit FlushPage also requests durable storage.
        disk_manager_.Flush();

        frame.is_dirty = false;
    }

    return true;
}

std::size_t BufferPool::FlushAllPages() {

    std::lock_guard<std::mutex> lock(mutex_);

    std::vector<frame_id_t> written_frames;

    for (std::size_t i = 0; i < pool_size_; ++i) {

        const frame_id_t frame_id =
            static_cast<frame_id_t>(i);

        Frame& frame = frames_[frame_id];

        if (!frame.valid || !frame.is_dirty) {
            continue;
        }

        std::unique_lock<std::shared_mutex> page_lock(frame.latch);

        WriteFrameLocked(frame_id);

        written_frames.push_back(frame_id);
    }

    if (written_frames.empty()) {
        return 0;
    }

    // Synchronize all page writes to durable storage.
    disk_manager_.Flush();

    // Mark clean only after successful synchronization.
    for (frame_id_t frame_id : written_frames) {
        frames_[frame_id].is_dirty = false;
    }

    return written_frames.size();
}

std::size_t BufferPool::GetPoolSize() const noexcept {
    return pool_size_;
}

std::size_t BufferPool::GetResidentPageCount() const {

    std::lock_guard<std::mutex> lock(mutex_);

    return page_table_.size();
}

std::size_t BufferPool::GetPinnedFrameCount() const {

    std::lock_guard<std::mutex> lock(mutex_);

    std::size_t count = 0;

    for (std::size_t i = 0; i < pool_size_; ++i) {

        if (frames_[i].valid && frames_[i].pin_count > 0) {
            ++count;
        }
    }

    return count;
}

std::size_t BufferPool::GetPinCount(page_id_t page_id) const {

    std::lock_guard<std::mutex> lock(mutex_);

    const auto it = page_table_.find(page_id);

    if (it == page_table_.end()) {
        return 0;
    }

    return frames_[it->second].pin_count;
}

bool BufferPool::IsPageResident(page_id_t page_id) const {

    std::lock_guard<std::mutex> lock(mutex_);

    return page_table_.find(page_id) != page_table_.end();
}

} // namespace minidb
