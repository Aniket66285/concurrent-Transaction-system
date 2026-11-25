
#include "minidb/recovery/checkpoint.hpp"

#include "minidb/common/exception.hpp"
#include "minidb/recovery/log_manager.hpp"
#include "minidb/recovery/log_record.hpp"

#include <array>
#include <cerrno>
#include <cstdint>
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

constexpr std::array<std::uint8_t, 4> kMasterMagic{
    {'M', 'D', 'B', 'M'}
};

constexpr std::uint8_t kMasterVersion = 1;

constexpr std::size_t kMasterRecordSize = 20;


// Writes a 64-bit integer in little-endian format.
void WriteUint64LE(
    std::vector<std::uint8_t>& bytes,
    std::uint64_t value
) {

    for (std::size_t i = 0; i < 8; ++i) {

        bytes.push_back(
            static_cast<std::uint8_t>(
                value & 0xFF
            )
        );

        value >>= 8;
    }
}


// Reads a 64-bit integer in little-endian format.
std::uint64_t ReadUint64LE(
    const std::uint8_t* bytes
) {

    std::uint64_t value = 0;

    for (std::size_t i = 0; i < 8; ++i) {

        value |=
            static_cast<std::uint64_t>(bytes[i])
            << (8 * i);
    }

    return value;
}


// Calculates CRC32 to detect corruption in master metadata.
std::uint32_t CalculateCRC32(
    const std::uint8_t* data,
    std::size_t length
) {

    std::uint32_t crc = 0xFFFFFFFF;

    for (std::size_t i = 0; i < length; ++i) {

        crc ^= data[i];

        for (int bit = 0; bit < 8; ++bit) {

            if (crc & 1) {
                crc = (crc >> 1) ^ 0xEDB88320;
            } else {
                crc >>= 1;
            }
        }
    }

    return crc ^ 0xFFFFFFFF;
}


// Writes all bytes, handling partial writes and EINTR.
void WriteAll(
    int fd,
    const std::uint8_t* data,
    std::size_t length
) {

    std::size_t totalWritten = 0;

    while (totalWritten < length) {

        const std::size_t remaining =
            length - totalWritten;

        const ssize_t result = ::write(
            fd,
            data + totalWritten,
            remaining
        );

        if (result < 0) {

            if (errno == EINTR) {
                continue;
            }

            throw StorageException(
                std::string("Master file write failed: ") +
                std::strerror(errno)
            );
        }

        if (result == 0) {
            throw StorageException(
                "Master file write returned zero bytes"
            );
        }

        totalWritten +=
            static_cast<std::size_t>(result);
    }
}


// Reads exactly the requested number of bytes.
bool ReadAll(
    int fd,
    std::uint8_t* data,
    std::size_t length
) {

    std::size_t totalRead = 0;

    while (totalRead < length) {

        const ssize_t result = ::read(
            fd,
            data + totalRead,
            length - totalRead
        );

        if (result < 0) {

            if (errno == EINTR) {
                continue;
            }

            throw StorageException(
                std::string("Master file read failed: ") +
                std::strerror(errno)
            );
        }

        if (result == 0) {
            return false;
        }

        totalRead +=
            static_cast<std::size_t>(result);
    }

    return true;
}


// Synchronizes a file descriptor with durable storage.
void SyncFile(int fd) {

    while (::fsync(fd) < 0) {

        if (errno == EINTR) {
            continue;
        }

        throw StorageException(
            std::string("Master file fsync failed: ") +
            std::strerror(errno)
        );
    }
}


// Creates the complete 20-byte master record.
std::vector<std::uint8_t> EncodeMasterRecord(
    lsn_t checkpoint_lsn
) {

    std::vector<std::uint8_t> bytes;

    bytes.reserve(kMasterRecordSize);

    // 1. Magic signature.
    bytes.insert(
        bytes.end(),
        kMasterMagic.begin(),
        kMasterMagic.end()
    );

    // 2. Version.
    bytes.push_back(kMasterVersion);

    // 3. Three reserved bytes.
    bytes.push_back(0);
    bytes.push_back(0);
    bytes.push_back(0);

    // 4. Checkpoint LSN.
    WriteUint64LE(bytes, checkpoint_lsn);

    // 5. CRC32 of the first 16 bytes.
    const std::uint32_t checksum =
        CalculateCRC32(bytes.data(), bytes.size());

    for (std::size_t i = 0; i < 4; ++i) {

        bytes.push_back(
            static_cast<std::uint8_t>(
                (checksum >> (8 * i)) & 0xFF
            )
        );
    }

    return bytes;
}

} // anonymous namespace


// --------------------------------------------------
// Constructor
// --------------------------------------------------

CheckpointManager::CheckpointManager(
    LogManager& log_manager,
    std::string master_file
)
    : log_manager_(log_manager),
      master_file_(std::move(master_file)) {

    if (master_file_.empty()) {
        throw StorageException(
            "Master file path cannot be empty"
        );
    }

    LoadMasterRecord();
}


// --------------------------------------------------
// LoadMasterRecord
// --------------------------------------------------

void CheckpointManager::LoadMasterRecord() {

    const int fd = ::open(
        master_file_.c_str(),
        O_RDONLY | O_CLOEXEC
    );

    // No master file means no previous checkpoint.
    if (fd < 0) {

        if (errno == ENOENT) {
            has_checkpoint_ = false;
            last_checkpoint_lsn_ = kInvalidLSN;
            return;
        }

        throw StorageException(
            std::string("Cannot open master file: ") +
            std::strerror(errno)
        );
    }

    try {

        struct stat fileInfo {};

        if (::fstat(fd, &fileInfo) < 0) {
            throw StorageException(
                std::string("Cannot inspect master file: ") +
                std::strerror(errno)
            );
        }

        if (fileInfo.st_size !=
            static_cast<off_t>(kMasterRecordSize)) {

            throw StorageException(
                "Invalid master file size"
            );
        }

        std::array<std::uint8_t, kMasterRecordSize> bytes{};

        if (!ReadAll(fd, bytes.data(), bytes.size())) {
            throw StorageException(
                "Incomplete master file"
            );
        }

        ::close(fd);

        // Validate magic signature.
        for (std::size_t i = 0; i < kMasterMagic.size(); ++i) {

            if (bytes[i] != kMasterMagic[i]) {
                throw StorageException(
                    "Invalid master file signature"
                );
            }
        }

        // Validate format version.
        if (bytes[4] != kMasterVersion) {
            throw StorageException(
                "Unsupported master file version"
            );
        }

        // Reserved bytes must remain zero.
        if (bytes[5] != 0 ||
            bytes[6] != 0 ||
            bytes[7] != 0) {

            throw StorageException(
                "Invalid master file reserved bytes"
            );
        }

        // Read checkpoint LSN.
        const lsn_t checkpointLSN =
            ReadUint64LE(bytes.data() + 8);

        if (checkpointLSN == kInvalidLSN) {
            throw StorageException(
                "Master file contains invalid checkpoint LSN"
            );
        }

        // Verify CRC32.
        const std::uint32_t storedChecksum =
            static_cast<std::uint32_t>(bytes[16]) |
            (static_cast<std::uint32_t>(bytes[17]) << 8) |
            (static_cast<std::uint32_t>(bytes[18]) << 16) |
            (static_cast<std::uint32_t>(bytes[19]) << 24);

        const std::uint32_t calculatedChecksum =
            CalculateCRC32(bytes.data(), 16);

        if (storedChecksum != calculatedChecksum) {
            throw StorageException(
                "Master file checksum mismatch"
            );
        }

        last_checkpoint_lsn_ = checkpointLSN;

        has_checkpoint_ = true;
    }
    catch (...) {

        ::close(fd);

        throw;
    }
}


// --------------------------------------------------
// PersistMasterRecord
// --------------------------------------------------

void CheckpointManager::PersistMasterRecord(
    lsn_t checkpoint_lsn
) const {

    if (checkpoint_lsn == kInvalidLSN) {
        throw StorageException(
            "Cannot persist invalid checkpoint LSN"
        );
    }

    const std::vector<std::uint8_t> bytes =
        EncodeMasterRecord(checkpoint_lsn);

    const std::filesystem::path path(master_file_);

    const auto parent = path.parent_path();

    // Create parent directory if required.
    if (!parent.empty()) {

        std::error_code error;

        std::filesystem::create_directories(
            parent,
            error
        );

        if (error) {
            throw StorageException(
                "Cannot create master directory: " +
                error.message()
            );
        }
    }

    // Write to a temporary file first.
    const std::string temporaryFile =
        master_file_ + ".tmp";

    int fd = ::open(
        temporaryFile.c_str(),
        O_WRONLY | O_CREAT | O_TRUNC | O_CLOEXEC,
        0644
    );

    if (fd < 0) {
        throw StorageException(
            std::string("Cannot create temporary master file: ") +
            std::strerror(errno)
        );
    }

    try {

        WriteAll(
            fd,
            bytes.data(),
            bytes.size()
        );

        SyncFile(fd);

        if (::close(fd) < 0) {
            fd = -1;

            throw StorageException(
                std::string("Cannot close temporary master file: ") +
                std::strerror(errno)
            );
        }

        fd = -1;

        // Atomic replacement of the master file.
        if (::rename(
                temporaryFile.c_str(),
                master_file_.c_str()
            ) < 0) {

            throw StorageException(
                std::string("Master file rename failed: ") +
                std::strerror(errno)
            );
        }

        // Synchronize the parent directory so that
        // the rename itself is durable.
        const std::string directoryPath =
            parent.empty() ? "." : parent.string();

        const int directoryFD = ::open(
            directoryPath.c_str(),
            O_RDONLY | O_DIRECTORY | O_CLOEXEC
        );

        if (directoryFD < 0) {
            throw StorageException(
                std::string("Cannot open master directory: ") +
                std::strerror(errno)
            );
        }

        try {

            SyncFile(directoryFD);

            ::close(directoryFD);
        }
        catch (...) {

            ::close(directoryFD);

            throw;
        }
    }
    catch (...) {

        if (fd >= 0) {
            ::close(fd);
        }

        ::unlink(temporaryFile.c_str());

        throw;
    }
}


// --------------------------------------------------
// CreateCheckpoint
// --------------------------------------------------

lsn_t CheckpointManager::CreateCheckpoint(
    const PageFlushCallback& flush_dirty_pages
) {

    std::lock_guard<std::mutex> lock(mutex_);

    if (!flush_dirty_pages) {
        throw StorageException(
            "Page flush callback is not provided"
        );
    }

    // STEP 1:
    // Make all existing WAL records durable.
    log_manager_.Flush();


    // STEP 2:
    // Flush all dirty database pages.
    // The caller must ensure transactions are quiesced.
    flush_dirty_pages();


    // STEP 3:
    // Create a system checkpoint record.
    LogRecord checkpointRecord;

    checkpointRecord.txn_id = 0;

    checkpointRecord.prev_lsn = kInvalidLSN;

    checkpointRecord.type = LogRecordType::CHECKPOINT;


    // STEP 4:
    // Append checkpoint record to WAL.
    const lsn_t checkpointLSN =
        log_manager_.AppendLogRecord(checkpointRecord);


    // STEP 5:
    // Make checkpoint record durable.
    log_manager_.FlushUpTo(checkpointLSN);


    // STEP 6:
    // Atomically store checkpoint pointer.
    PersistMasterRecord(checkpointLSN);


    // Update in-memory checkpoint information.
    last_checkpoint_lsn_ = checkpointLSN;

    has_checkpoint_ = true;

    return checkpointLSN;
}


// --------------------------------------------------
// GetLastCheckpointLSN
// --------------------------------------------------

lsn_t CheckpointManager::GetLastCheckpointLSN() const {

    std::lock_guard<std::mutex> lock(mutex_);

    return last_checkpoint_lsn_;
}


// --------------------------------------------------
// HasCheckpoint
// --------------------------------------------------

bool CheckpointManager::HasCheckpoint() const {

    std::lock_guard<std::mutex> lock(mutex_);

    return has_checkpoint_;
}

} // namespace minidb
