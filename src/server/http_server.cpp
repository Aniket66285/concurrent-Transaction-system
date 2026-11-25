#include "minidb/server/http_server.hpp"

#include <algorithm>
#include <cerrno>
#include <chrono>
#include <cstring>
#include <limits>
#include <sstream>
#include <stdexcept>
#include <string>
#include <utility>

#include <arpa/inet.h>
#include <netinet/in.h>
#include <sys/socket.h>
#include <unistd.h>
#include <cctype>
#include <sys/time.h>

namespace minidb {
namespace {

constexpr std::size_t kMaximumHeaderBytes = 16 * 1024;
constexpr std::size_t kMaximumBodyBytes = 2 * 1024 * 1024;

struct HttpRequest {
    std::string method;
    std::string path;
    std::string body;
};

std::string Trim(const std::string& value) {
    const auto first = value.find_first_not_of(" \t\r\n");

    if (first == std::string::npos) {
        return {};
    }

    const auto last = value.find_last_not_of(" \t\r\n");

    return value.substr(first, last - first + 1);
}

std::string Lowercase(std::string value) {
    std::transform(
        value.begin(),
        value.end(),
        value.begin(),
        [](unsigned char ch) {
            return static_cast<char>(std::tolower(ch));
        }
    );

    return value;
}

bool ReadHttpRequest(int socket, HttpRequest& request) {
    std::string received;
    char buffer[4096];

    std::size_t header_end = std::string::npos;

    // Read HTTP headers.
    while ((header_end = received.find("\r\n\r\n")) ==
           std::string::npos) {

        const ssize_t count = recv(
            socket,
            buffer,
            sizeof(buffer),
            0
        );

        if (count == 0) {
            return false;
        }

        if (count < 0) {
            if (errno == EINTR) {
                continue;
            }

            return false;
        }

        received.append(buffer, static_cast<std::size_t>(count));

        if (received.size() > kMaximumHeaderBytes) {
            return false;
        }
    }

    const std::string header_text =
        received.substr(0, header_end);

    std::istringstream headers(header_text);

    std::string request_line;

    if (!std::getline(headers, request_line)) {
        return false;
    }

    if (!request_line.empty() && request_line.back() == '\r') {
        request_line.pop_back();
    }

    std::istringstream first_line(request_line);

    std::string http_version;

    if (!(first_line >> request.method >>
          request.path >> http_version)) {
        return false;
    }

    if (http_version.rfind("HTTP/", 0) != 0) {
        return false;
    }

    std::size_t content_length = 0;
    std::string line;

    while (std::getline(headers, line)) {
        if (!line.empty() && line.back() == '\r') {
            line.pop_back();
        }

        const auto separator = line.find(':');

        if (separator == std::string::npos) {
            continue;
        }

        const std::string name = Lowercase(
            Trim(line.substr(0, separator))
        );

        const std::string value =
            Trim(line.substr(separator + 1));

        if (name == "content-length") {
            try {
                std::size_t parsed = 0;

                const auto length = std::stoull(value, &parsed);

                if (parsed != value.size() ||
                    length > kMaximumBodyBytes) {
                    return false;
                }

                content_length = static_cast<std::size_t>(length);
            } catch (const std::exception&) {
                return false;
            }
        }
    }

    const std::size_t body_start = header_end + 4;

    // Read until the complete body arrives.
    while (received.size() - body_start < content_length) {
        const ssize_t count = recv(
            socket,
            buffer,
            sizeof(buffer),
            0
        );

        if (count == 0) {
            return false;
        }

        if (count < 0) {
            if (errno == EINTR) {
                continue;
            }

            return false;
        }

        received.append(buffer, static_cast<std::size_t>(count));

        if (received.size() - body_start > kMaximumBodyBytes) {
            return false;
        }
    }

    request.body = received.substr(body_start, content_length);

    // Remove query parameters from the route path.
    const auto query_position = request.path.find('?');

    if (query_position != std::string::npos) {
        request.path.resize(query_position);
    }

    return true;
}

const char* StatusText(int status) {
    switch (status) {
        case 200: return "OK";
        case 201: return "Created";
        case 204: return "No Content";
        case 400: return "Bad Request";
        case 404: return "Not Found";
        case 500: return "Internal Server Error";
        default:  return "Response";
    }
}

bool SendAll(int socket, const std::string& data) {
    std::size_t sent = 0;

    while (sent < data.size()) {
        const ssize_t count = send(
            socket,
            data.data() + sent,
            data.size() - sent,
            MSG_NOSIGNAL
        );

        if (count < 0) {
            if (errno == EINTR) {
                continue;
            }

            return false;
        }

        if (count == 0) {
            return false;
        }

        sent += static_cast<std::size_t>(count);
    }

    return true;
}

void SendResponse(int socket, const ApiResponse& response) {
    std::ostringstream output;

    output << "HTTP/1.1 "
           << response.status_code << ' '
           << StatusText(response.status_code) << "\r\n";

    output << "Content-Type: application/json; charset=utf-8\r\n";
    output << "Content-Length: " << response.body.size() << "\r\n";
    output << "Connection: close\r\n";
    output << "Access-Control-Allow-Origin: *\r\n";
    output << "Access-Control-Allow-Methods: GET, POST, DELETE, OPTIONS\r\n";
    output << "Access-Control-Allow-Headers: Content-Type\r\n";
    output << "\r\n";
    output << response.body;

    SendAll(socket, output.str());
}

} // namespace

HttpServer::HttpServer(
    SqlApi& api,
    WorkerPool& workers,
    const std::string& host,
    std::uint16_t port
)
    : api_(api),
      workers_(workers),
      host_(host),
      requested_port_(port) {}

HttpServer::~HttpServer() {
    Stop();
}

void HttpServer::Start() {
    std::lock_guard<std::mutex> lock(lifecycle_mutex_);

    if (running_) {
        throw std::logic_error("HTTP server is already running");
    }

    const int server_socket = socket(
        AF_INET,
        SOCK_STREAM,
        0
    );

    if (server_socket < 0) {
        throw std::runtime_error(
            std::string("socket failed: ") + std::strerror(errno)
        );
    }

    int reuse_address = 1;

    if (setsockopt(
            server_socket,
            SOL_SOCKET,
            SO_REUSEADDR,
            &reuse_address,
            sizeof(reuse_address)
        ) < 0) {

        const int error = errno;
        close(server_socket);

        throw std::runtime_error(
            std::string("setsockopt failed: ") +
            std::strerror(error)
        );
    }

    sockaddr_in address{};
    address.sin_family = AF_INET;
    address.sin_port = htons(requested_port_);

    if (inet_pton(
            AF_INET,
            host_.c_str(),
            &address.sin_addr
        ) != 1) {

        close(server_socket);

        throw std::invalid_argument(
            "HTTP host must be a valid IPv4 address"
        );
    }

    if (bind(
            server_socket,
            reinterpret_cast<sockaddr*>(&address),
            sizeof(address)
        ) < 0) {

        const int error = errno;
        close(server_socket);

        throw std::runtime_error(
            std::string("bind failed: ") + std::strerror(error)
        );
    }

    if (listen(server_socket, 64) < 0) {
        const int error = errno;
        close(server_socket);

        throw std::runtime_error(
            std::string("listen failed: ") + std::strerror(error)
        );
    }

    socklen_t address_length = sizeof(address);

    if (getsockname(
            server_socket,
            reinterpret_cast<sockaddr*>(&address),
            &address_length
        ) < 0) {

        const int error = errno;
        close(server_socket);

        throw std::runtime_error(
            std::string("getsockname failed: ") +
            std::strerror(error)
        );
    }

    bound_port_ = ntohs(address.sin_port);
    listen_socket_ = server_socket;
    running_ = true;

    try {
        accept_thread_ = std::thread(
            &HttpServer::AcceptLoop,
            this,
            server_socket
        );
    } catch (...) {
        running_ = false;
        listen_socket_ = -1;
        close(server_socket);
        throw;
    }
}

void HttpServer::Stop() {
    std::lock_guard<std::mutex> lock(lifecycle_mutex_);

    running_ = false;

    const int socket_to_close = listen_socket_;
    listen_socket_ = -1;

    if (socket_to_close >= 0) {
        shutdown(socket_to_close, SHUT_RDWR);
        close(socket_to_close);
    }

    if (accept_thread_.joinable()) {
        if (accept_thread_.get_id() ==
            std::this_thread::get_id()) {
            throw std::logic_error(
                "HTTP server cannot join its own accept thread"
            );
        }

        accept_thread_.join();
    }
}

bool HttpServer::IsRunning() const noexcept {
    return running_.load();
}

std::uint16_t HttpServer::GetBoundPort() const {
    std::lock_guard<std::mutex> lock(lifecycle_mutex_);
    return bound_port_;
}

void HttpServer::AcceptLoop(int server_socket) {
    while (running_) {
        const int client_socket = accept(
            server_socket,
            nullptr,
            nullptr
        );

        if (client_socket < 0) {
            if (errno == EINTR) {
                continue;
            }

            if (!running_) {
                break;
            }

            continue;
        }

        if (!running_) {
            close(client_socket);
            break;
        }

        try {
            auto task = workers_.Submit(
                [this, client_socket]() {
                    HandleClient(client_socket);
                }
            );

            // The HTTP task is handled by the worker pool.
            (void)task;

        } catch (...) {
            close(client_socket);
        }
    }
}

void HttpServer::HandleClient(int client_socket) {
    timeval timeout{};
    timeout.tv_sec = 10;
    timeout.tv_usec = 0;

    setsockopt(
        client_socket,
        SOL_SOCKET,
        SO_RCVTIMEO,
        &timeout,
        sizeof(timeout)
    );

    setsockopt(
        client_socket,
        SOL_SOCKET,
        SO_SNDTIMEO,
        &timeout,
        sizeof(timeout)
    );

    HttpRequest request;

    try {
        if (!ReadHttpRequest(client_socket, request)) {
            ApiResponse response{
                400,
                "{\"error\":\"Invalid or incomplete HTTP request\"}"
            };

            SendResponse(client_socket, response);
            close(client_socket);
            return;
        }

        const ApiResponse response = api_.Handle(
            request.method,
            request.path,
            request.body
        );

        SendResponse(client_socket, response);

    } catch (const std::exception&) {
        SendResponse(
            client_socket,
            ApiResponse{
                500,
                "{\"error\":\"Internal server error\"}"
            }
        );
    }

    close(client_socket);
}

} // namespace minidb