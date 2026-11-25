
#pragma once

#include "minidb/common/config.hpp"
#include "minidb/common/types.hpp"

#include <array>
#include <cstddef>
#include <cstdint>
#include <mutex>
#include <string>

namespace minidb {

class DiskManager {
public:

    // Exactly one database page (4096 bytes).
    using PageData =
        std::array<std::uint8_t, Config::kPageSizeBytes>;

    // Opens an existing database or creates a new one.
    explicit DiskManager(
        std::string file_path = Config::kDatabaseFile
    );

    ~DiskManager() noexcept;

    DiskManager(const DiskManager&) = delete;

    DiskManager& operator=(const DiskManager&) = delete;

    // Allocates a new zero-initialized page.
    // Returns its page ID.
    page_id_t AllocatePage();

    // Reads one allocated page from disk.
    void ReadPage(
        page_id_t page_id,
        PageData& destination
    ) const;

    // Writes one existing page to disk.
    void WritePage(
        page_id_t page_id,
        const PageData& source
    );

    // Synchronizes database changes to durable storage.
    void Flush();

    // Returns the number of allocated pages.
    std::uint64_t GetPageCount() const;

    // Checks whether a page ID has been allocated.
    bool IsPageAllocated(page_id_t page_id) const;

    // Returns the database file path.
    std::string GetFilePath() const;

private:

    // Validates the page while mutex_ is held.
    void EnsurePageExistsLocked(page_id_t page_id) const;

    int fd_{-1};

    std::string file_path_;

    mutable std::mutex mutex_;

    // Number of complete 4096-byte pages in the file.
    std::uint64_t page_count_{0};
};

} // namespace minidb
