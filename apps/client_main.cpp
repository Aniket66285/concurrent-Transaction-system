#include <arpa/inet.h>
#include <netinet/in.h>
#include <sys/socket.h>
#include <sys/time.h>
#include <unistd.h>

#include <cerrno>
#include <cstdint>
#include <cstdlib>
#include <cstring>
#include <iostream>
#include <sstream>
#include <stdexcept>
#include <string>
#include <utility>

namespace {

std::string JsonEscape(const std::string& value) {
    std::ostringstream out;
    for (unsigned char ch : value) {
        switch (ch) {
            case '"': out << "\\\""; break;
            case '\\': out << "\\\\"; break;
            case '\b': out << "\\b"; break;
            case '\f': out << "\\f"; break;
            case '\n': out << "\\n"; break;
            case '\r': out << "\\r"; break;
            case '\t': out << "\\t"; break;
            default:
                if (ch < 0x20) out << '?';
                else out << static_cast<char>(ch);
        }
    }
    return out.str();
}

bool SendAll(int fd, const std::string& data) {
    std::size_t offset = 0;
    while (offset < data.size()) {
        const auto sent = send(fd, data.data() + offset,
                               data.size() - offset, MSG_NOSIGNAL);
        if (sent < 0) {
            if (errno == EINTR) continue;
            return false;
        }
        if (sent == 0) return false;
        offset += static_cast<std::size_t>(sent);
    }
    return true;
}

struct HttpResponse {
    int status{0};
    std::string body;
};

HttpResponse HttpRequest(
    const std::string& host,
    unsigned short port,
    const std::string& method,
    const std::string& path,
    const std::string& body
) {
    const int fd = socket(AF_INET, SOCK_STREAM, 0);
    if (fd < 0) throw std::runtime_error("Could not create client socket");

    timeval timeout{};
    timeout.tv_sec = 130;
    setsockopt(fd, SOL_SOCKET, SO_RCVTIMEO, &timeout, sizeof(timeout));
    setsockopt(fd, SOL_SOCKET, SO_SNDTIMEO, &timeout, sizeof(timeout));

    sockaddr_in address{};
    address.sin_family = AF_INET;
    address.sin_port = htons(port);
    if (inet_pton(AF_INET, host.c_str(), &address.sin_addr) != 1) {
        close(fd);
        throw std::invalid_argument("MINIDB_HOST must be an IPv4 address");
    }

    if (connect(fd, reinterpret_cast<sockaddr*>(&address), sizeof(address)) < 0) {
        const std::string error = std::string("Cannot connect to MiniDB at ") +
                                  host + ":" + std::to_string(port) +
                                  " (" + std::strerror(errno) + ")";
        close(fd);
        throw std::runtime_error(error);
    }

    std::ostringstream request;
    request << method << ' ' << path << " HTTP/1.1\r\n"
            << "Host: " << host << ':' << port << "\r\n"
            << "Content-Type: application/json\r\n"
            << "Content-Length: " << body.size() << "\r\n"
            << "Connection: close\r\n\r\n"
            << body;

    if (!SendAll(fd, request.str())) {
        close(fd);
        throw std::runtime_error("Failed to send request to MiniDB");
    }

    std::string received;
    char buffer[8192];
    while (true) {
        const auto count = recv(fd, buffer, sizeof(buffer), 0);
        if (count == 0) break;
        if (count < 0) {
            if (errno == EINTR) continue;
            const std::string error = std::string("HTTP receive failed: ") + std::strerror(errno);
            close(fd);
            throw std::runtime_error(error);
        }
        received.append(buffer, static_cast<std::size_t>(count));
        if (received.size() > 4 * 1024 * 1024) {
            close(fd);
            throw std::runtime_error("MiniDB HTTP response exceeded 4 MiB limit");
        }
    }
    close(fd);

    const auto header_end = received.find("\r\n\r\n");
    if (header_end == std::string::npos) {
        throw std::runtime_error("MiniDB returned an invalid HTTP response");
    }
    const auto first_end = received.find("\r\n");
    const std::string status_line = received.substr(0, first_end);
    std::istringstream status_stream(status_line);
    std::string protocol;
    HttpResponse response;
    status_stream >> protocol >> response.status;
    if (response.status == 0) throw std::runtime_error("Could not parse HTTP status");
    response.body = received.substr(header_end + 4);
    return response;
}

std::uint64_t ExtractSessionId(const std::string& json) {
    const std::string key = "\"session_id\":";
    const auto position = json.find(key);
    if (position == std::string::npos) {
        throw std::runtime_error("Session creation response has no session_id: " + json);
    }
    const auto start = position + key.size();
    std::size_t consumed = 0;
    const auto id = std::stoull(json.substr(start), &consumed);
    if (consumed == 0) throw std::runtime_error("Invalid session_id response");
    return id;
}

bool EndsWithStatementTerminator(const std::string& sql) {
    const auto position = sql.find_last_not_of(" \t\r\n");
    return position != std::string::npos && sql[position] == ';';
}

} // namespace

int main() {
    const char* host_env = std::getenv("MINIDB_HOST");
    const std::string host = host_env ? host_env : "127.0.0.1";

    unsigned long configured_port = 8080;
    if (const char* port_env = std::getenv("MINIDB_PORT")) {
        try {
            configured_port = std::stoul(port_env);
        } catch (...) {
            std::cerr << "MINIDB_PORT must be an integer.\n";
            return 2;
        }
    }
    if (configured_port == 0 || configured_port > 65535) {
        std::cerr << "MINIDB_PORT must be between 1 and 65535.\n";
        return 2;
    }
    const auto port = static_cast<unsigned short>(configured_port);

    std::uint64_t session_id = 0;
    try {
        const auto created = HttpRequest(host, port, "POST", "/api/sessions", "{}");
        if (created.status < 200 || created.status >= 300) {
            throw std::runtime_error("Session creation failed: " + created.body);
        }
        session_id = ExtractSessionId(created.body);
    } catch (const std::exception& error) {
        std::cerr << error.what() << '\n';
        return 1;
    }

    std::cout << "MiniDB SQL Client\n"
              << "Connected to " << host << ':' << port
              << " | Session S" << session_id << "\n"
              << "Type SQL ending with ';'. Commands: :commit, :rollback, :help, :quit\n\n";

    std::string sql;
    std::string line;
    while (true) {
        std::cout << (sql.empty() ? "minidb> " : "      -> ") << std::flush;
        if (!std::getline(std::cin, line)) break;

        if (sql.empty() && !line.empty() && line.front() == ':') {
            if (line == ":quit" || line == ":q" || line == ":exit") break;
            if (line == ":help") {
                std::cout << "Enter SQL ending in semicolon. Example: SELECT * FROM students;\n"
                          << ":commit commits the open transaction; :rollback reverses it; :quit exits.\n";
                continue;
            }
            if (line == ":commit") line = "COMMIT;";
            else if (line == ":rollback") line = "ROLLBACK;";
            else {
                std::cout << "Unknown command. Type :help.\n";
                continue;
            }
        }

        sql += line;
        sql.push_back('\n');
        if (!EndsWithStatementTerminator(sql)) continue;

        try {
            const std::string body = "{\"session_id\":" + std::to_string(session_id) +
                                     ",\"sql\":\"" + JsonEscape(sql) + "\"}";
            const auto response = HttpRequest(host, port, "POST", "/api/sql", body);
            std::cout << "HTTP " << response.status << '\n' << response.body << "\n\n";
        } catch (const std::exception& error) {
            std::cout << "ERROR: " << error.what() << "\n\n";
        }
        sql.clear();
    }

    if (!sql.empty()) {
        std::cout << "Discarding unterminated SQL input.\n";
    }

    try {
        (void)HttpRequest(host, port, "DELETE",
                          "/api/sessions/" + std::to_string(session_id), "");
    } catch (...) {
        // The server may already be offline.
    }
    std::cout << "Session S" << session_id << " closed. Bye.\n";
    return 0;
}
