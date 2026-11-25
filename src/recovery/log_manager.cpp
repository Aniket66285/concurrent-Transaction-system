
#include "minidb/recovery/log_manager.hpp"

#include "minidb/common/exception.hpp"
#include "minidb/recovery/binary_log_record_codec.hpp"

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
#include <vector>

namespace minidb {

namespace {

// Every encoded record has a 40-byte header.
constexpr std::size_t kWalHeaderSize = 40;


// Reads exactly the requested number of bytes from a file offset.
bool ReadExactlyAt(
    int fd,
    std::uint8_t* destination,
    std::size_t length,
    std::uint64_t offset
) {
    std::size_t totalRead = 0;

    while (totalRead < length) {

        const std::uint64_t maxOffset =
            static_cast<std::uint64_t>(
                std::numeric_limits<off_t>::max()
            );

        if (offset > maxOffset - totalRead) {
            throw StorageException(
                "WAL read offset exceeds file limit"
            );
        }

        const std::uint64_t currentOffset =
            offset + totalRead;

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
            static_cast<off_t>(currentOffset)
        );

        if (result < 0) {

            if (errno == EINTR) {
                continue;
            }

            throw StorageException(
                std::string("WAL read failed: ") +
                std::strerror(errno)
            );
        }

        if (result == 0) {
            return false;
        }

        totalRead += static_cast<std::size_t>(result);
    }

    return true;
}


// Reads a 32-bit little-endian integer.
std::uint32_t ReadUint32LE(
    const std::uint8_t* data
) {

    return
        static_cast<std::uint32_t>(data[0]) |
        (static_cast<std::uint32_t>(data[1]) << 8) |
        (static_cast<std::uint32_t>(data[2]) << 16) |
        (static_cast<std::uint32_t>(data[3]) << 24);
}

} // anonymous namespace


// --------------------------------------------------
// Constructor
// --------------------------------------------------

LogManager::LogManager(std::string file_path)
    : file_path_(std::move(file_path)) {

    if (file_path_.empty()) {
        throw StorageException(
            "WAL file path cannot be empty"
        );
    }

    // Create the parent directory if necessary.
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
                "Cannot create WAL directory: " +
                error.message()
            );
        }
    }

    // Open the WAL file for both reading and writing.
    fd_ = ::open(
        file_path_.c_str(),
        O_RDWR | O_CREAT | O_APPEND | O_CLOEXEC,
        0644
    );

    if (fd_ < 0) {
        throw StorageException(
            std::string("Cannot open WAL file: ") +
            std::strerror(errno)
        );
    }

    // Find the current size of the WAL file.
    struct stat fileInfo {};

    if (::fstat(fd_, &fileInfo) < 0) {

        const int savedError = errno;

        ::close(fd_);
        fd_ = -1;

        throw StorageException(
            std::string("Cannot inspect WAL file: ") +
            std::strerror(savedError)
        );
    }

    if (fileInfo.st_size < 0) {

        ::close(fd_);
        fd_ = -1;

        throw StorageException(
            "Invalid WAL file size"
        );
    }

    // The next LSN is the current end of the WAL file.
    next_lsn_ = static_cast<lsn_t>(fileInfo.st_size);
}


// --------------------------------------------------
// Destructor
// --------------------------------------------------

LogManager::~LogManager() noexcept {

    if (fd_ >= 0) {
        ::close(fd_);
        fd_ = -1;
    }
}


// --------------------------------------------------
// AppendLogRecord
// --------------------------------------------------

lsn_t LogManager::AppendLogRecord(
    const LogRecord& record
) {

    // Convert the LogRecord into binary bytes.
    // Invalid records are rejected by the codec.
    const std::vector<std::uint8_t> bytes =
        EncodeLogRecord(record);

    std::lock_guard<std::mutex> lock(mutex_);

    if (fd_ < 0) {
        throw StorageException(
            "WAL file is not open"
        );
    }

    if (write_failed_) {
        throw StorageException(
            "WAL writer is in a failed state"
        );
    }

    // Prevent LSN overflow.
    if (bytes.size() >
        std::numeric_limits<lsn_t>::max() - next_lsn_) {

        throw StorageException(
            "WAL file offset overflow"
        );
    }

    // Starting byte offset of this record.
    const lsn_t recordLSN = next_lsn_;

    std::size_t written = 0;

    // Handle partial writes and interrupted system calls.
    while (written < bytes.size()) {

        const std::size_t remaining =
            bytes.size() - written;

        const std::size_t chunkSize = std::min(
            remaining,
            static_cast<std::size_t>(
                std::numeric_limits<ssize_t>::max()
            )
        );

        const ssize_t result = ::write(
            fd_,
            bytes.data() + written,
            chunkSize
        );

        if (result < 0) {

            if (errno == EINTR) {
                continue;
            }

            write_failed_ = true;

            throw StorageException(
                std::string("WAL write failed: ") +
                std::strerror(errno)
            );
        }

        if (result == 0) {

            write_failed_ = true;

            throw StorageException(
                "WAL write returned zero bytes"
            );
        }

        written += static_cast<std::size_t>(result);
    }

    // Update the file position after the complete write.
    next_lsn_ += static_cast<lsn_t>(bytes.size());

    last_record_lsn_ = recordLSN;

    return recordLSN;
}


// --------------------------------------------------
// FlushLocked
// --------------------------------------------------

void LogManager::FlushLocked() {

    if (fd_ < 0) {
        throw StorageException(
            "WAL file is not open"
        );
    }

    // fsync synchronizes file data to durable storage.
    while (::fsync(fd_) < 0) {

        if (errno == EINTR) {
            continue;
        }

        throw StorageException(
            std::string("WAL fsync failed: ") +
            std::strerror(errno)
        );
    }

    if (last_record_lsn_ != kInvalidLSN) {
        persistent_lsn_ = last_record_lsn_;
    }
}


// --------------------------------------------------
// Flush
// --------------------------------------------------

void LogManager::Flush() {

    std::lock_guard<std::mutex> lock(mutex_);

    FlushLocked();
}


// --------------------------------------------------
// FlushUpTo
// --------------------------------------------------

void LogManager::FlushUpTo(lsn_t target_lsn) {

    std::lock_guard<std::mutex> lock(mutex_);

    if (target_lsn == kInvalidLSN) {
        throw StorageException(
            "Cannot flush an invalid LSN"
        );
    }

    if (last_record_lsn_ == kInvalidLSN ||
        target_lsn > last_record_lsn_) {

        throw StorageException(
            "Requested LSN has not been appended"
        );
    }

    // No work is needed if the target is already durable.
    if (persistent_lsn_ != kInvalidLSN &&
        target_lsn <= persistent_lsn_) {
        return;
    }

    FlushLocked();
}


// --------------------------------------------------
// ReadAllRecords
// --------------------------------------------------

std::vector<LogRecord> LogManager::ReadAllRecords(
    bool repair_incomplete_tail
) {

    std::lock_guard<std::mutex> lock(mutex_);

    if (fd_ < 0) {
        throw StorageException(
            "WAL file is not open"
        );
    }

    struct stat fileInfo {};

    if (::fstat(fd_, &fileInfo) < 0) {
        throw StorageException(
            std::string("Cannot inspect WAL: ") +
            std::strerror(errno)
        );
    }

    if (fileInfo.st_size < 0) {
        throw StorageException(
            "Invalid WAL file size"
        );
    }

    const std::uint64_t fileSize =
        static_cast<std::uint64_t>(fileInfo.st_size);

    std::uint64_t offset = 0;

    lsn_t lastValidLSN = kInvalidLSN;

    bool tailRepaired = false;

    std::vector<LogRecord> records;


    // Repairs an incomplete final record when permitted.
    auto repairTail = [&]() {

        if (!repair_incomplete_tail) {
            throw StorageException(
                "Incomplete final WAL record found"
            );
        }

        if (offset >
            static_cast<std::uint64_t>(
                std::numeric_limits<off_t>::max()
            )) {

            throw StorageException(
                "WAL truncation offset exceeds file limit"
            );
        }

        // Remove incomplete trailing bytes.
        if (::ftruncate(
                fd_,
                static_cast<off_t>(offset)
            ) < 0) {

            throw StorageException(
                std::string("WAL truncation failed: ") +
                std::strerror(errno)
            );
        }

        // Make the repaired file length durable.
        while (::fsync(fd_) < 0) {

            if (errno == EINTR) {
                continue;
            }

            throw StorageException(
                std::string("WAL fsync failed: ") +
                std::strerror(errno)
            );
        }

        tailRepaired = true;
    };


    // Read records one by one.
    while (offset < fileSize) {

        const std::uint64_t remaining =
            fileSize - offset;

        // A complete header must contain 40 bytes.
        if (remaining < kWalHeaderSize) {
            repairTail();
            break;
        }

        std::vector<std::uint8_t> header(
            kWalHeaderSize
        );

        if (!ReadExactlyAt(
                fd_,
                header.data(),
                header.size(),
                offset
            )) {

            repairTail();
            break;
        }

        // Verify magic signature.
        if (header[0] != 'M' ||
            header[1] != 'D' ||
            header[2] != 'B' ||
            header[3] != 'L') {

            throw StorageException(
                "Invalid WAL magic signature"
            );
        }

        // Verify format version.
        if (header[4] != 1) {
            throw StorageException(
                "Unsupported WAL format version"
            );
        }

        // Before-image length is stored at byte offset 32.
        const std::uint32_t beforeLength =
            ReadUint32LE(header.data() + 32);

        // After-image length is stored at byte offset 36.
        const std::uint32_t afterLength =
            ReadUint32LE(header.data() + 36);

        const std::uint64_t recordSize =
            kWalHeaderSize +
            static_cast<std::uint64_t>(beforeLength) +
            static_cast<std::uint64_t>(afterLength);


        // Header exists, but the payload is incomplete.
        if (recordSize > remaining) {
            repairTail();
            break;
        }

        if (recordSize >
            std::numeric_limits<std::size_t>::max()) {

            throw StorageException(
                "WAL record is too large to decode"
            );
        }

        std::vector<std::uint8_t> bytes(
            static_cast<std::size_t>(recordSize)
        );

        if (!ReadExactlyAt(
                fd_,
                bytes.data(),
                bytes.size(),
                offset
            )) {

            repairTail();
            break;
        }

        // Decode and validate the record.
        LogRecord record = DecodeLogRecord(bytes);

        // The record's starting byte offset is its LSN.
        record.lsn = offset;

        lastValidLSN = record.lsn;

        records.push_back(std::move(record));

        offset += recordSize;
    }


    // Set the next append position.
    next_lsn_ = offset;

    last_record_lsn_ = lastValidLSN;

    if (tailRepaired) {

        // All retained records were synchronized during repair.
        persistent_lsn_ = lastValidLSN;

        // A new LogManager instance can continue appending.
        write_failed_ = false;
    }

    return records;
}


// --------------------------------------------------
// GetNextLSN
// --------------------------------------------------

lsn_t LogManager::GetNextLSN() const {

    std::lock_guard<std::mutex> lock(mutex_);

    return next_lsn_;
}


// --------------------------------------------------
// GetPersistentLSN
// --------------------------------------------------

lsn_t LogManager::GetPersistentLSN() const {

    std::lock_guard<std::mutex> lock(mutex_);

    return persistent_lsn_;
}

} // namespace minidb
