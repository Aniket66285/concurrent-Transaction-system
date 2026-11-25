#include "minidb/os/procfs_reader.hpp"

#include <cassert>
#include <iostream>
#include <sys/syscall.h>
#include <unistd.h>

int main() {
    const auto tid = static_cast<pid_t>(syscall(SYS_gettid));
    const auto snapshot = minidb::ProcfsReader::ReadThread(tid);
    assert(snapshot.tid == tid);
    assert(snapshot.readable);
    std::cout << "Current Linux thread procfs read: PASS\n";
    std::cout << "TID: " << snapshot.tid << '\n';
    std::cout << "Last CPU core: " << snapshot.last_cpu_core << '\n';
    std::cout << "CPU time (ms): " << snapshot.cpu_time_ms << '\n';
    std::cout << "Context switches: voluntary="
              << snapshot.voluntary_context_switches
              << ", nonvoluntary="
              << snapshot.nonvoluntary_context_switches << '\n';
    return 0;
}
