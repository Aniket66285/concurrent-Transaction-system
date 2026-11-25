#include "minidb/os/procfs_reader.hpp"

#include <fstream>
#include <sstream>
#include <string>
#include <vector>
#include <unistd.h>

namespace minidb {
namespace {

bool ParseUnsigned(const std::string& value, std::uint64_t& output) {
    try {
        std::size_t consumed = 0;
        const auto parsed = std::stoull(value, &consumed);
        if (consumed != value.size()) return false;
        output = parsed;
        return true;
    } catch (...) {
        return false;
    }
}

} // namespace

LinuxThreadSnapshot ProcfsReader::ReadThread(pid_t tid) {
    LinuxThreadSnapshot snapshot;
    snapshot.tid = tid;

#if defined(__linux__)
    const std::string base = "/proc/self/task/" + std::to_string(tid) + "/";

    // /proc/<pid>/task/<tid>/stat contains command name in parentheses.
    // Use the final ')' so spaces or ')' inside the name do not shift fields.
    std::ifstream stat_file(base + "stat");
    std::string stat_line;
    if (!stat_file || !std::getline(stat_file, stat_line)) return snapshot;

    const auto close_paren = stat_line.rfind(')');
    if (close_paren == std::string::npos || close_paren + 2 >= stat_line.size()) {
        return snapshot;
    }

    std::istringstream fields_stream(stat_line.substr(close_paren + 2));
    std::vector<std::string> fields;
    std::string field;
    while (fields_stream >> field) fields.push_back(field);

    // Fields begin at proc stat field 3 (state):
    // utime=field 14 => index 11, stime=field 15 => index 12,
    // processor=field 39 => index 36.
    if (fields.size() <= 36) return snapshot;

    std::uint64_t user_ticks = 0;
    std::uint64_t system_ticks = 0;
    std::uint64_t cpu_ticks = 0;
    if (!ParseUnsigned(fields[11], user_ticks) ||
        !ParseUnsigned(fields[12], system_ticks)) {
        return snapshot;
    }

    try {
        snapshot.last_cpu_core = std::stoi(fields[36]);
    } catch (...) {
        snapshot.last_cpu_core = -1;
    }

    const long ticks_per_second = sysconf(_SC_CLK_TCK);
    if (ticks_per_second > 0) {
        cpu_ticks = user_ticks + system_ticks;
        snapshot.cpu_time_ms =
            (cpu_ticks * 1000ULL) / static_cast<std::uint64_t>(ticks_per_second);
    }

    std::ifstream status_file(base + "status");
    std::string line;
    while (status_file && std::getline(status_file, line)) {
        constexpr const char* voluntary = "voluntary_ctxt_switches:";
        constexpr const char* involuntary = "nonvoluntary_ctxt_switches:";
        if (line.rfind(voluntary, 0) == 0) {
            ParseUnsigned(line.substr(std::char_traits<char>::length(voluntary)),
                          snapshot.voluntary_context_switches);
        } else if (line.rfind(involuntary, 0) == 0) {
            ParseUnsigned(line.substr(std::char_traits<char>::length(involuntary)),
                          snapshot.nonvoluntary_context_switches);
        }
    }

    snapshot.readable = true;
#endif
    return snapshot;
}

} // namespace minidb
