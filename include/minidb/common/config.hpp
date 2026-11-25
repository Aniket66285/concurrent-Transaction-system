
#pragma once

#include <cstddef>
#include <cstdint>

namespace minidb {

struct Config {

    static constexpr std::size_t kPageSizeBytes = 4096;

    static constexpr std::size_t kBufferPoolFrames = 4;

    static constexpr std::size_t kWorkerThreads = 3;

    static constexpr std::size_t kRequestQueueCapacity = 8;

    static constexpr const char* kDatabaseFile =
        "data/minidb.db";

    static constexpr const char* kWalFile =
        "data/minidb.wal";

    static constexpr const char* kMasterFile =
        "data/minidb.master";
};

} // namespace minidb
