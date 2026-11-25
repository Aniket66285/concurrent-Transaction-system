
#pragma once

#include "minidb/buffer/lru_replacer.hpp"
#include "minidb/buffer/page_guard.hpp"
#include "minidb/common/config.hpp"
#include "minidb/storage/disk_manager.hpp"

#include <cstddef>
#include <functional>
#include <memory>
#include <mutex>
#include <shared_mutex>
#include <unordered_map>
#include <vector>

namespace minidb {

class BufferPool {
public:
    using WalFlushCallback = std::function<void()>;

    explicit BufferPool(
        DiskManager& disk_manager,
        std::size_t pool_size = Config::kBufferPoolFrames,
        WalFlushCallback wal_flush_callback = {}
    );

    ~BufferPool() noexcept;

    BufferPool(const BufferPool&) = delete;
    BufferPool& operator=(const BufferPool&) = delete;

    // Allocate a new database page and pin it.
    // Returns an invalid guard if no frame is available.
    PageGuard NewPage(page_id_t* page_id = nullptr);

    // Load an existing page and pin it.
    // Throws if the requested page does not exist on disk.
    PageGuard FetchPage(page_id_t page_id);

    // Flush one resident page to disk.
    bool FlushPage(page_id_t page_id);

    // Flush all dirty resident pages.
    // Returns the number of pages written.
    std::size_t FlushAllPages();

    std::size_t GetPoolSize() const noexcept;

    std::size_t GetResidentPageCount() const;

    std::size_t GetPinnedFrameCount() const;

    std::size_t GetPinCount(page_id_t page_id) const;

    bool IsPageResident(page_id_t page_id) const;

private:
    friend class PageGuard;

    struct Frame {
        DiskManager::PageData data{};

        page_id_t page_id{0};

        std::size_t pin_count{0};

        bool is_dirty{false};

        bool valid{false};

        // Protects actual page contents.
        mutable std::shared_mutex latch;
    };

    static std::size_t ValidatePoolSize(std::size_t pool_size);

    bool AcquireFrameLocked(frame_id_t& frame_id);

    void ResetFrameLocked(frame_id_t frame_id);

    void PinFrameLocked(frame_id_t frame_id);

    void WriteFrameLocked(frame_id_t frame_id);

    void UnpinFrame(
        frame_id_t frame_id,
        page_id_t page_id,
        bool is_dirty
    ) noexcept;

    DiskManager& disk_manager_;

    const std::size_t pool_size_;

    std::unique_ptr<Frame[]> frames_;

    std::unordered_map<page_id_t, frame_id_t> page_table_;

    std::vector<frame_id_t> free_list_;

    LRUReplacer replacer_;

    WalFlushCallback wal_flush_callback_;

    mutable std::mutex mutex_;
};

} // namespace minidb
