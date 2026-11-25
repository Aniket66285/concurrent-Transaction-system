
#include "minidb/storage/disk_manager.hpp"

#include "minidb/common/exception.hpp"

#include <algorithm>
#include <cerrno>
#include <cstring>
#include <filesystem>
#include <fcntl.h>
#include <limits>
#include <string>
#include <sys/stat.h>
#include <unistd.h>
#include <utility>

namespace minidb {

namespace {

// --------------------------------------------------
// CalculatePageOffset
// --------------------------------------------------

off_t CalculatePageOffset(page_id_t page_id) {

    const std::uint64_t offset =
        static_cast<std::uint64_t>(page_id) *
        Config::kPageSizeBytes;

    if (offset >
        static_cast<std::uint64_t>(
            std::numeric_limits<off_t>::max()
        )) {

        throw StorageException(
            "Page offset exceeds filesystem limit"
        );
    }

    return static_cast<off_t>(offset);
}


// --------------------------------------------------
// ReadExactlyAt
// --------------------------------------------------

void ReadExactlyAt(
    int fd,
    std::uint8_t* destination,
    std::size_t length,
    off_t offset
) {

    std::size_t totalRead = 0;

    while (totalRead < length) {

        const std::size_t chunkSize = std::min(
            length - totalRead,
            static_cast<std::size_t>(
                std::numeric_limits<ssize_t>::max()
            )
        );

        const ssize_t result = ::pread(
            fd,
            destination + totalRead,
            chunkSize,
            offset + static_cast<off_t>(totalRead)
        );


        if (result < 0) {

            if (errno == EINTR) {
                continue;
            }

            throw StorageException(
                std::string("Database page read failed: ") +
                std::strerror(errno)
            );
        }


        if (result == 0) {

            throw StorageException(
                "Unexpected end of database file"
            );
        }

        totalRead += static_cast<std::size_t>(result);
    }
}


// --------------------------------------------------
// WriteExactlyAt
// --------------------------------------------------

void WriteExactlyAt(
    int fd,
    const std::uint8_t* source,
    std::size_t length,
    off_t offset
) {

    std::size_t totalWritten = 0;

    while (totalWritten < length) {

        const std::size_t chunkSize = std::min(
            length - totalWritten,
            static_cast<std::size_t>(
                std::numeric_limits<ssize_t>::max()
            )
        );

        const ssize_t result = ::pwrite(
            fd,
            source + totalWritten,
            chunkSize,
            offset + static_cast<off_t>(totalWritten)
        );


        if (result < 0) {

            if (errno == EINTR) {
                continue;
            }

            throw StorageException(
                std::string("Database page write failed: ") +
                std::strerror(errno)
            );
        }


        if (result == 0) {

            throw StorageException(
                "Database page write returned zero bytes"
            );
        }

        totalWritten += static_cast<std::size_t>(result);
    }
}


// --------------------------------------------------
// SyncFile
// --------------------------------------------------

void SyncFile(int fd) {

    while (::fsync(fd) < 0) {

        if (errno == EINTR) {
            continue;
        }

        throw StorageException(
            std::string("Database file fsync failed: ") +
            std::strerror(errno)
        );
    }
}

} // anonymous namespace


// --------------------------------------------------
// Constructor
// --------------------------------------------------

DiskManager::DiskManager(std::string file_path)
    : file_path_(std::move(file_path)) {

    if (file_path_.empty()) {

        throw StorageException(
            "Database file path cannot be empty"
        );
    }


    // Create the parent directory if required.
    const std::filesystem::path path(file_path_);

    const auto parent = path.parent_path();

    if (!parent.empty()) {

        std::error_code error;

        std::filesystem::create_directories(
            parent,
            error
        );

        if (error) {

            throw StorageException(
                "Cannot create database directory: " +
                error.message()
            );
        }
    }


    // Open the database file for reading and writing.
    fd_ = ::open(
        file_path_.c_str(),
        O_RDWR | O_CREAT | O_CLOEXEC,
        0644
    );

    if (fd_ < 0) {

        throw StorageException(
            std::string("Cannot open database file: ") +
            std::strerror(errno)
        );
    }


    // Determine the existing file size.
    struct stat fileInfo {};

    if (::fstat(fd_, &fileInfo) < 0) {

        const int savedError = errno;

        ::close(fd_);

        fd_ = -1;

        throw StorageException(
            std::string("Cannot inspect database file: ") +
            std::strerror(savedError)
        );
    }


    if (fileInfo.st_size < 0) {

        ::close(fd_);

        fd_ = -1;

        throw StorageException(
            "Invalid database file size"
        );
    }


    const std::uint64_t fileSize =
        static_cast<std::uint64_t>(fileInfo.st_size);


    // Database files must contain complete 4096-byte pages.
    if (fileSize % Config::kPageSizeBytes != 0) {

        ::close(fd_);

        fd_ = -1;

        throw StorageException(
            "Database file size is not page aligned"
        );
    }


    page_count_ =
        fileSize / Config::kPageSizeBytes;


    // Page ID zero through UINT32_MAX are representable.
    const std::uint64_t maximumPageCount =
        static_cast<std::uint64_t>(
            std::numeric_limits<page_id_t>::max()
        ) + 1;

    if (page_count_ > maximumPageCount) {

        ::close(fd_);

        fd_ = -1;

        throw StorageException(
            "Database contains too many pages"
        );
    }
}


// --------------------------------------------------
// Destructor
// --------------------------------------------------

DiskManager::~DiskManager() noexcept {

    if (fd_ >= 0) {

        ::close(fd_);

        fd_ = -1;
    }
}


// --------------------------------------------------
// EnsurePageExistsLocked
// --------------------------------------------------

void DiskManager::EnsurePageExistsLocked(
    page_id_t page_id
) const {

    if (static_cast<std::uint64_t>(page_id) >= page_count_) {

        throw StorageException(
            "Requested database page does not exist"
        );
    }
}


// --------------------------------------------------
// AllocatePage
// --------------------------------------------------

page_id_t DiskManager::AllocatePage() {

    std::lock_guard<std::mutex> lock(mutex_);


    if (fd_ < 0) {

        throw StorageException(
            "Database file is not open"
        );
    }


    const std::uint64_t maximumPageId =
        std::numeric_limits<page_id_t>::max();


    if (page_count_ > maximumPageId) {

        throw StorageException(
            "No more page IDs are available"
        );
    }


    // Calculate the current end of the file.
    const std::uint64_t fileOffset =
        page_count_ * Config::kPageSizeBytes;


    const std::uint64_t newFileSize =
        fileOffset + Config::kPageSizeBytes;


    if (newFileSize >
        static_cast<std::uint64_t>(
            std::numeric_limits<off_t>::max()
        )) {

        throw StorageException(
            "Database file reached filesystem size limit"
        );
    }


    // Extending the file with ftruncate creates a
    // zero-initialized range for the new page.
    if (::ftruncate(
            fd_,
            static_cast<off_t>(newFileSize)
        ) < 0) {

        throw StorageException(
            std::string("Page allocation failed: ") +
            std::strerror(errno)
        );
    }


    // The page ID is its position in the file.
    const page_id_t newPageId =
        static_cast<page_id_t>(page_count_);

    ++page_count_;

    return newPageId;
}


// --------------------------------------------------
// ReadPage
// --------------------------------------------------

void DiskManager::ReadPage(
    page_id_t page_id,
    PageData& destination
) const {

    std::lock_guard<std::mutex> lock(mutex_);


    if (fd_ < 0) {

        throw StorageException(
            "Database file is not open"
        );
    }


    EnsurePageExistsLocked(page_id);


    const off_t offset =
        CalculatePageOffset(page_id);


    ReadExactlyAt(
        fd_,
        destination.data(),
        destination.size(),
        offset
    );
}


// --------------------------------------------------
// WritePage
// --------------------------------------------------

void DiskManager::WritePage(
    page_id_t page_id,
    const PageData& source
) {

    std::lock_guard<std::mutex> lock(mutex_);


    if (fd_ < 0) {

        throw StorageException(
            "Database file is not open"
        );
    }


    EnsurePageExistsLocked(page_id);


    const off_t offset =
        CalculatePageOffset(page_id);


    WriteExactlyAt(
        fd_,
        source.data(),
        source.size(),
        offset
    );
}


// --------------------------------------------------
// Flush
// --------------------------------------------------

void DiskManager::Flush() {

    std::lock_guard<std::mutex> lock(mutex_);


    if (fd_ < 0) {

        throw StorageException(
            "Database file is not open"
        );
    }


    SyncFile(fd_);
}


// --------------------------------------------------
// GetPageCount
// --------------------------------------------------

std::uint64_t DiskManager::GetPageCount() const {

    std::lock_guard<std::mutex> lock(mutex_);

    return page_count_;
}


// --------------------------------------------------
// IsPageAllocated
// --------------------------------------------------

bool DiskManager::IsPageAllocated(
    page_id_t page_id
) const {

    std::lock_guard<std::mutex> lock(mutex_);

    return static_cast<std::uint64_t>(page_id) <
           page_count_;
}


// --------------------------------------------------
// GetFilePath
// --------------------------------------------------

std::string DiskManager::GetFilePath() const {

    return file_path_;
}


} // namespace minidb
