
#include "minidb/buffer/page_guard.hpp"

#include "minidb/buffer/buffer_pool.hpp"
#include "minidb/common/exception.hpp"

#include <utility>

namespace minidb {

PageGuard::PageGuard(
    BufferPool* buffer_pool,
    frame_id_t frame_id,
    page_id_t page_id,
    DiskManager::PageData* page_data,
    std::shared_mutex& page_latch
)
    : buffer_pool_(buffer_pool),
      frame_id_(frame_id),
      page_id_(page_id),
      page_data_(page_data),
      is_dirty_(false),
      latch_(page_latch) {
}

PageGuard::~PageGuard() noexcept {
    Drop();
}

PageGuard::PageGuard(PageGuard&& other) noexcept
    : buffer_pool_(other.buffer_pool_),
      frame_id_(other.frame_id_),
      page_id_(other.page_id_),
      page_data_(other.page_data_),
      is_dirty_(other.is_dirty_),
      latch_(std::move(other.latch_)) {

    other.buffer_pool_ = nullptr;
    other.page_data_ = nullptr;
    other.is_dirty_ = false;
}

PageGuard& PageGuard::operator=(PageGuard&& other) noexcept {

    if (this != &other) {

        Drop();

        buffer_pool_ = other.buffer_pool_;
        frame_id_ = other.frame_id_;
        page_id_ = other.page_id_;
        page_data_ = other.page_data_;
        is_dirty_ = other.is_dirty_;

        latch_ = std::move(other.latch_);

        other.buffer_pool_ = nullptr;
        other.page_data_ = nullptr;
        other.is_dirty_ = false;
    }

    return *this;
}

bool PageGuard::IsValid() const noexcept {
    return buffer_pool_ != nullptr && page_data_ != nullptr;
}

PageGuard::operator bool() const noexcept {
    return IsValid();
}

page_id_t PageGuard::GetPageId() const noexcept {
    return page_id_;
}

frame_id_t PageGuard::GetFrameId() const noexcept {
    return frame_id_;
}

bool PageGuard::IsDirty() const noexcept {
    return is_dirty_;
}

std::uint8_t* PageGuard::GetData() noexcept {

    if (!IsValid()) {
        return nullptr;
    }

    is_dirty_ = true;

    return page_data_->data();
}

const std::uint8_t* PageGuard::GetData() const noexcept {

    if (!IsValid()) {
        return nullptr;
    }

    return page_data_->data();
}

DiskManager::PageData& PageGuard::GetPageData() {

    if (!IsValid()) {
        throw StorageException(
            "Cannot access data through an invalid PageGuard"
        );
    }

    is_dirty_ = true;

    return *page_data_;
}

const DiskManager::PageData& PageGuard::GetPageData() const {

    if (!IsValid()) {
        throw StorageException(
            "Cannot access data through an invalid PageGuard"
        );
    }

    return *page_data_;
}

void PageGuard::MarkDirty() {

    if (!IsValid()) {
        throw StorageException(
            "Cannot mark an invalid PageGuard as dirty"
        );
    }

    is_dirty_ = true;
}

void PageGuard::Drop() noexcept {

    if (!IsValid()) {
        return;
    }

    // Release the page latch BEFORE decreasing the pin count.
    // Otherwise another thread could evict a frame while its
    // previous guard still holds the page latch.

    if (latch_.owns_lock()) {
        latch_.unlock();
    }

    BufferPool* pool = buffer_pool_;

    const frame_id_t frame = frame_id_;
    const page_id_t page = page_id_;
    const bool dirty = is_dirty_;

    buffer_pool_ = nullptr;
    page_data_ = nullptr;
    is_dirty_ = false;

    pool->UnpinFrame(frame, page, dirty);
}

} // namespace minidb
