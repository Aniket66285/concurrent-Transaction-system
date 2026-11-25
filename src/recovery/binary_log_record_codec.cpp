
#include "minidb/recovery/binary_log_record_codec.hpp"
#include "minidb/common/exception.hpp"

#include <array>
#include <cstddef>
#include <cstdint>
#include <limits>
#include <vector>

namespace minidb {

namespace {

// Every encoded record starts with this signature.
constexpr std::array<std::uint8_t, 4> kMagic{
    {'M', 'D', 'B', 'L'}
};

constexpr std::uint8_t kFormatVersion = 1;

// Header size: 4 + 1 + 1 + 1 + 1 + 8 + 8
//              + 4 + 2 + 2 + 4 + 4 = 40 bytes.
constexpr std::uint64_t kHeaderSize = 40;

// Writes an integer in little-endian format.
void WriteLittleEndian(
    std::vector<std::uint8_t>& output,
    std::uint64_t value,
    std::size_t width
) {
    for (std::size_t i = 0; i < width; ++i) {
        output.push_back(
            static_cast<std::uint8_t>(value & 0xFF)
        );

        value >>= 8;
    }
}

// Reads an integer in little-endian format.
std::uint64_t ReadLittleEndian(
    const std::vector<std::uint8_t>& input,
    std::size_t& offset,
    std::size_t width
) {
    if (width > 8 ||
        offset > input.size() ||
        width > input.size() - offset) {

        throw StorageException(
            "Unexpected end of binary log record"
        );
    }

    std::uint64_t value = 0;

    for (std::size_t i = 0; i < width; ++i) {

        value |=
            static_cast<std::uint64_t>(input[offset++])
            << (8 * i);
    }

    return value;
}

} // anonymous namespace


std::vector<std::uint8_t> EncodeLogRecord(
    const LogRecord& record
) {

    if (!IsWellFormedLogRecord(record)) {
        throw StorageException(
            "Cannot encode an invalid LogRecord"
        );
    }

    const auto maxLength =
        std::numeric_limits<std::uint32_t>::max();

    if (record.before_image.size() > maxLength ||
        record.after_image.size() > maxLength) {

        throw StorageException(
            "LogRecord image is too large"
        );
    }

    const std::uint64_t totalSize =
        kHeaderSize +
        record.before_image.size() +
        record.after_image.size();

    if (totalSize >
        std::numeric_limits<std::size_t>::max()) {

        throw StorageException(
            "Encoded LogRecord is too large"
        );
    }

    std::vector<std::uint8_t> output;

    output.reserve(static_cast<std::size_t>(totalSize));

    // 1. Magic signature.
    output.insert(
        output.end(),
        kMagic.begin(),
        kMagic.end()
    );

    // 2. Format version.
    WriteLittleEndian(output, kFormatVersion, 1);

    // 3. Record type.
    WriteLittleEndian(
        output,
        static_cast<std::uint8_t>(record.type),
        1
    );

    // 4. Flags: bit 0 represents has_page.
    WriteLittleEndian(
        output,
        record.has_page ? 1 : 0,
        1
    );

    // 5. Reserved byte.
    WriteLittleEndian(output, 0, 1);

    // 6. Transaction ID.
    WriteLittleEndian(output, record.txn_id, 8);

    // 7. Previous LSN.
    WriteLittleEndian(output, record.prev_lsn, 8);

    // 8. Page ID.
    WriteLittleEndian(output, record.page_id, 4);

    // 9. Slot ID.
    WriteLittleEndian(output, record.slot_id, 2);

    // 10. Reserved two bytes.
    WriteLittleEndian(output, 0, 2);

    // 11. Before image length.
    WriteLittleEndian(
        output,
        record.before_image.size(),
        4
    );

    // 12. After image length.
    WriteLittleEndian(
        output,
        record.after_image.size(),
        4
    );

    // 13. Before image data.
    output.insert(
        output.end(),
        record.before_image.begin(),
        record.before_image.end()
    );

    // 14. After image data.
    output.insert(
        output.end(),
        record.after_image.begin(),
        record.after_image.end()
    );

    return output;
}


LogRecord DecodeLogRecord(
    const std::vector<std::uint8_t>& bytes
) {

    if (bytes.size() < kHeaderSize) {
        throw StorageException(
            "Binary LogRecord is smaller than its header"
        );
    }

    // Verify magic signature.
    for (std::size_t i = 0; i < kMagic.size(); ++i) {

        if (bytes[i] != kMagic[i]) {
            throw StorageException(
                "Invalid LogRecord magic signature"
            );
        }
    }

    std::size_t offset = 4;

    // 1. Verify format version.
    const auto version =
        ReadLittleEndian(bytes, offset, 1);

    if (version != kFormatVersion) {
        throw StorageException(
            "Unsupported LogRecord format version"
        );
    }

    LogRecord record;

    // 2. Record type.
    record.type = static_cast<LogRecordType>(
        ReadLittleEndian(bytes, offset, 1)
    );

    // 3. Flags.
    const auto flags =
        ReadLittleEndian(bytes, offset, 1);

    if ((flags & ~std::uint64_t{1}) != 0) {
        throw StorageException(
            "Unknown LogRecord flags"
        );
    }

    record.has_page = (flags & 1) != 0;

    // 4. Reserved byte.
    const auto reserved1 =
        ReadLittleEndian(bytes, offset, 1);

    if (reserved1 != 0) {
        throw StorageException(
            "Invalid reserved field"
        );
    }

    // 5. Transaction ID.
    record.txn_id =
        ReadLittleEndian(bytes, offset, 8);

    // 6. Previous LSN.
    record.prev_lsn =
        ReadLittleEndian(bytes, offset, 8);

    // 7. Page ID.
    record.page_id = static_cast<page_id_t>(
        ReadLittleEndian(bytes, offset, 4)
    );

    // 8. Slot ID.
    record.slot_id = static_cast<slot_id_t>(
        ReadLittleEndian(bytes, offset, 2)
    );

    // 9. Reserved two bytes.
    const auto reserved2 =
        ReadLittleEndian(bytes, offset, 2);

    if (reserved2 != 0) {
        throw StorageException(
            "Invalid reserved field"
        );
    }

    // 10. Image lengths.
    const auto beforeLength =
        ReadLittleEndian(bytes, offset, 4);

    const auto afterLength =
        ReadLittleEndian(bytes, offset, 4);

    const std::uint64_t expectedSize =
        kHeaderSize + beforeLength + afterLength;

    if (expectedSize != bytes.size()) {
        throw StorageException(
            "LogRecord payload length mismatch"
        );
    }

    // 11. Read before image.
    const auto beforeEnd =
        offset + static_cast<std::size_t>(beforeLength);

    record.before_image.assign(
        bytes.begin() + offset,
        bytes.begin() + beforeEnd
    );

    offset = beforeEnd;

    // 12. Read after image.
    const auto afterEnd =
        offset + static_cast<std::size_t>(afterLength);

    record.after_image.assign(
        bytes.begin() + offset,
        bytes.begin() + afterEnd
    );

    // LSN is assigned later by LogManager.
    record.lsn = kInvalidLSN;

    if (!IsWellFormedLogRecord(record)) {
        throw StorageException(
            "Decoded LogRecord is invalid"
        );
    }

    return record;
}

} // namespace minidb
