
#pragma once

#include "minidb/recovery/log_record.hpp"

#include <cstdint>
#include <vector>

namespace minidb {

// Converts an in-memory LogRecord into binary bytes.
std::vector<std::uint8_t> EncodeLogRecord(
    const LogRecord& record
);

// Reconstructs a LogRecord from binary bytes.
LogRecord DecodeLogRecord(
    const std::vector<std::uint8_t>& bytes
);

} // namespace minidb
