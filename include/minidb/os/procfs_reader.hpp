#pragma once

#include <cstdint>
#include <sys/types.h>

namespace minidb {

struct LinuxThreadSnapshot {
    pid_t tid{-1};
    int last_cpu_core{-1};
    std::uint64_t cpu_time_ms{0};
    std::uint64_t voluntary_context_switches{0};
    std::uint64_t nonvoluntary_context_switches{0};
    bool readable{false};
};

class ProcfsReader {
public:
    // Reads cumulative kernel counters for one thread in this process.
    // last_cpu_core is the last CPU reported by /proc, not a guarantee
    // that the thread is executing on that core at this exact instant.
    static LinuxThreadSnapshot ReadThread(pid_t tid);
};

} // namespace minidb
