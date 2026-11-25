
#pragma once

#include "minidb/storage/disk_manager.hpp"
#include "minidb/common/types.hpp"

#include <cstdint>
#include <memory>
#include <mutex>
#include <shared_mutex>

namespace minidb {

class BufferPool;

class PageGuard {
public:
    PageGuard() noexcept = default;

    ~PageGuard() noexcept;

    PageGuard(const PageGuard&) = delete;
    PageGuard& operator=(const PageGuard&) = delete;

    PageGuard(PageGuard&& other) noexcept;
    PageGuard& operator=(PageGuard&& other) noexcept;

    bool IsValid() const noexcept;

    explicit operator bool() const noexcept;

    page_id_t GetPageId() const noexcept;

    frame_id_t GetFrameId() const noexcept;

    bool IsDirty() const noexcept;

    // Mutable access automatically marks the page dirty.
    std::uint8_t* GetData() noexcept;

    // Const access does not mark the page dirty.
    const std::uint8_t* GetData() const noexcept;

    DiskManager::PageData& GetPageData();

    const DiskManager::PageData& GetPageData() const;

    void MarkDirty();

    void Drop() noexcept;

private:
    friend class BufferPool;

    PageGuard(
        BufferPool* buffer_pool,
        frame_id_t frame_id,
        page_id_t page_id,
        DiskManager::PageData* page_data,
        std::shared_mutex& page_latch
    );

    BufferPool* buffer_pool_{nullptr};

    frame_id_t frame_id_{0};

    page_id_t page_id_{0};

    DiskManager::PageData* page_data_{nullptr};

    bool is_dirty_{false};

    // Initial implementation uses an exclusive page latch.
    std::unique_lock<std::shared_mutex> latch_;
};

} // namespace minidb
