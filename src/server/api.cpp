#include "minidb/server/api.hpp"
#include "minidb/os/scheduler_experiment.hpp"

#include <cctype>
#include <cmath>
#include <cstdint>
#include <iomanip>
#include <map>
#include <mutex>
#include <sstream>
#include <stdexcept>
#include <string>
#include <type_traits>
#include <utility>
#include <variant>
#include <vector>
#include <cctype>

namespace minidb {
namespace {

// ---------- JSON utilities ----------

std::string EscapeJson(const std::string& value) {
    std::ostringstream out;

    for (unsigned char ch : value) {
        switch (ch) {
            case '"':  out << "\\\""; break;
            case '\\': out << "\\\\"; break;
            case '\b': out << "\\b"; break;
            case '\f': out << "\\f"; break;
            case '\n': out << "\\n"; break;
            case '\r': out << "\\r"; break;
            case '\t': out << "\\t"; break;

            default:
                if (ch < 0x20) {
                    out << "\\u"
                        << std::hex << std::setw(4)
                        << std::setfill('0')
                        << static_cast<unsigned int>(ch)
                        << std::dec;
                } else {
                    out << static_cast<char>(ch);
                }
        }
    }

    return out.str();
}

std::string JsonString(const std::string& value) {
    return "\"" + EscapeJson(value) + "\"";
}

template <typename T>
struct IsVariant : std::false_type {};

template <typename... Types>
struct IsVariant<std::variant<Types...>> : std::true_type {};

template <typename T>
std::string JsonValue(const T& value) {
    using ValueType = std::decay_t<T>;

    if constexpr (IsVariant<ValueType>::value) {
        return std::visit(
            [](const auto& item) {
                return JsonValue(item);
            },
            value
        );
    } else if constexpr (
        std::is_same_v<ValueType, std::string>
    ) {
        return JsonString(value);
    } else if constexpr (
        std::is_same_v<ValueType, std::monostate>
    ) {
        return "null";
    } else if constexpr (
        std::is_same_v<ValueType, bool>
    ) {
        return value ? "true" : "false";
    } else if constexpr (
        std::is_floating_point_v<ValueType>
    ) {
        if (!std::isfinite(value)) {
            return "null";
        }

        std::ostringstream out;
        out << std::setprecision(15) << value;
        return out.str();
    } else if constexpr (
        std::is_arithmetic_v<ValueType>
    ) {
        return std::to_string(value);
    } else {
        std::ostringstream out;
        out << value;
        return JsonString(out.str());
    }
}

// ---------- Minimal JSON object parser ----------

struct JsonScalar {
    std::variant<std::string, std::uint64_t> value;
};

class JsonObjectParser {
public:
    explicit JsonObjectParser(const std::string& input)
        : input_(input) {}

    std::map<std::string, JsonScalar> Parse() {
        SkipWhitespace();
        Expect('{');

        std::map<std::string, JsonScalar> object;

        SkipWhitespace();

        if (Consume('}')) {
            return object;
        }

        while (true) {
            SkipWhitespace();
            std::string key = ParseString();

            SkipWhitespace();
            Expect(':');
            SkipWhitespace();

            JsonScalar value;

            if (Peek() == '"') {
                value.value = ParseString();
            } else {
                value.value = ParseUnsignedInteger();
            }

            if (!object.emplace(key, std::move(value)).second) {
                throw std::runtime_error("Duplicate JSON key");
            }

            SkipWhitespace();

            if (Consume('}')) {
                break;
            }

            Expect(',');
        }

        SkipWhitespace();

        if (position_ != input_.size()) {
            throw std::runtime_error("Unexpected JSON content");
        }

        return object;
    }

private:
    char Peek() const {
        if (position_ >= input_.size()) {
            throw std::runtime_error("Unexpected end of JSON");
        }

        return input_[position_];
    }

    void SkipWhitespace() {
        while (position_ < input_.size() &&
               std::isspace(
                   static_cast<unsigned char>(input_[position_])
               )) {
            ++position_;
        }
    }

    bool Consume(char expected) {
        if (position_ < input_.size() &&
            input_[position_] == expected) {
            ++position_;
            return true;
        }

        return false;
    }

    void Expect(char expected) {
        if (!Consume(expected)) {
            throw std::runtime_error("Invalid JSON structure");
        }
    }

    static int HexDigit(char ch) {
        if (ch >= '0' && ch <= '9') return ch - '0';
        if (ch >= 'a' && ch <= 'f') return ch - 'a' + 10;
        if (ch >= 'A' && ch <= 'F') return ch - 'A' + 10;
        return -1;
    }

    unsigned int ParseHex4() {
        if (input_.size() - position_ < 4) {
            throw std::runtime_error("Invalid Unicode escape");
        }

        unsigned int codepoint = 0;

        for (int i = 0; i < 4; ++i) {
            int digit = HexDigit(input_[position_++]);

            if (digit < 0) {
                throw std::runtime_error("Invalid Unicode escape");
            }

            codepoint = codepoint * 16 +
                        static_cast<unsigned int>(digit);
        }

        return codepoint;
    }

    static void AppendUtf8(
        std::string& output,
        unsigned int codepoint
    ) {
        if (codepoint >= 0xD800 && codepoint <= 0xDFFF) {
            throw std::runtime_error(
                "Unicode surrogate pair is not supported"
            );
        }

        if (codepoint <= 0x7F) {
            output.push_back(static_cast<char>(codepoint));
        } else if (codepoint <= 0x7FF) {
            output.push_back(
                static_cast<char>(0xC0 | (codepoint >> 6))
            );
            output.push_back(
                static_cast<char>(0x80 | (codepoint & 0x3F))
            );
        } else {
            output.push_back(
                static_cast<char>(0xE0 | (codepoint >> 12))
            );
            output.push_back(
                static_cast<char>(0x80 | ((codepoint >> 6) & 0x3F))
            );
            output.push_back(
                static_cast<char>(0x80 | (codepoint & 0x3F))
            );
        }
    }

    std::string ParseString() {
        Expect('"');

        std::string result;

        while (true) {
            if (position_ >= input_.size()) {
                throw std::runtime_error("Unterminated JSON string");
            }

            unsigned char ch =
                static_cast<unsigned char>(input_[position_++]);

            if (ch == '"') {
                break;
            }

            if (ch < 0x20) {
                throw std::runtime_error(
                    "Invalid control character in JSON string"
                );
            }

            if (ch != '\\') {
                result.push_back(static_cast<char>(ch));
                continue;
            }

            if (position_ >= input_.size()) {
                throw std::runtime_error("Invalid JSON escape");
            }

            char escaped = input_[position_++];

            switch (escaped) {
                case '"':  result.push_back('"'); break;
                case '\\': result.push_back('\\'); break;
                case '/':  result.push_back('/'); break;
                case 'b':  result.push_back('\b'); break;
                case 'f':  result.push_back('\f'); break;
                case 'n':  result.push_back('\n'); break;
                case 'r':  result.push_back('\r'); break;
                case 't':  result.push_back('\t'); break;

                case 'u': {
                    unsigned int codepoint = ParseHex4();
                    AppendUtf8(result, codepoint);
                    break;
                }

                default:
                    throw std::runtime_error("Invalid JSON escape");
            }
        }

        return result;
    }

    std::uint64_t ParseUnsignedInteger() {
        std::size_t start = position_;

        while (position_ < input_.size() &&
               std::isdigit(
                   static_cast<unsigned char>(input_[position_])
               )) {
            ++position_;
        }

        if (start == position_) {
            throw std::runtime_error(
                "Expected a string or unsigned integer"
            );
        }

        try {
            return std::stoull(
                input_.substr(start, position_ - start)
            );
        } catch (const std::exception&) {
            throw std::runtime_error("Invalid JSON integer");
        }
    }

    const std::string& input_;
    std::size_t position_{0};
};

const JsonScalar& GetRequired(
    const std::map<std::string, JsonScalar>& object,
    const std::string& key
) {
    auto it = object.find(key);

    if (it == object.end()) {
        throw std::runtime_error("Missing JSON field: " + key);
    }

    return it->second;
}

std::uint64_t GetUnsigned(
    const std::map<std::string, JsonScalar>& object,
    const std::string& key
) {
    const auto& value = GetRequired(object, key).value;

    if (!std::holds_alternative<std::uint64_t>(value)) {
        throw std::runtime_error(
            "JSON field must be an unsigned integer: " + key
        );
    }

    return std::get<std::uint64_t>(value);
}

std::string GetString(
    const std::map<std::string, JsonScalar>& object,
    const std::string& key
) {
    const auto& value = GetRequired(object, key).value;

    if (!std::holds_alternative<std::string>(value)) {
        throw std::runtime_error(
            "JSON field must be a string: " + key
        );
    }

    return std::get<std::string>(value);
}

ApiResponse ErrorResponse(
    int status,
    const std::string& message
) {
    return {
        status,
        "{\"error\":" + JsonString(message) + "}"
    };
}

std::string SessionIdFromPath(const std::string& path) {
    const std::string prefix = "/api/sessions/";

    if (path.rfind(prefix, 0) != 0) {
        return {};
    }

    std::string id = path.substr(prefix.size());

    if (id.empty()) {
        return {};
    }

    for (unsigned char ch : id) {
        if (!std::isdigit(ch)) {
            return {};
        }
    }

    return id;
}

} // namespace

// ---------- SQL API ----------

SqlApi::SqlApi(SqlEngine& engine)
    : engine_(engine) {}

void SqlApi::SetMetricsProvider(MetricsProvider provider) {
    std::lock_guard<std::mutex> lock(metrics_mutex_);
    metrics_provider_ = std::move(provider);
}

void SqlApi::SetSchedulerExperiment(SchedulerExperiment* experiment) {
    std::lock_guard<std::mutex> lock(metrics_mutex_);
    scheduler_experiment_ = experiment;
}

ApiResponse SqlApi::Handle(
    const std::string& method,
    const std::string& path,
    const std::string& body
) {
    try {
        if (method == "GET" && path == "/api/health") {
            return {
                200,
                "{\"status\":\"ok\",\"service\":\"MiniDB\"}"
            };
        }

        if (method == "GET" && path == "/api/sessions") {
            const auto ids = engine_.GetSessionIds();

            std::ostringstream json;
            json << "{\"sessions\":[";

            for (std::size_t i = 0; i < ids.size(); ++i) {
                if (i != 0) {
                    json << ',';
                }

                json << ids[i];
            }

            json << "],\"count\":" << ids.size();

            const auto details = engine_.GetSessionSnapshots();
            json << ",\"session_details\":[";
            for (std::size_t i = 0; i < details.size(); ++i) {
                if (i) json << ',';
                const auto& item = details[i];
                json << "{\"session_id\":" << item.session_id
                     << ",\"has_transaction\":"
                     << (item.has_transaction ? "true" : "false")
                     << ",\"session_busy\":"
                     << (item.session_busy ? "true" : "false")
                     << ",\"transaction_id\":" << item.transaction_id
                     << ",\"transaction_state\":"
                     << JsonString(item.transaction_state) << '}';
            }
            json << "]}";

            return {200, json.str()};
        }

        if (method == "POST" && path == "/api/sessions") {
            const auto id = engine_.OpenSession();

            return {
                201,
                "{\"session_id\":" + std::to_string(id) + "}"
            };
        }

        if (method == "DELETE") {
            const std::string id_text = SessionIdFromPath(path);

            if (id_text.empty()) {
                return ErrorResponse(404, "Session endpoint not found");
            }

            const auto id = static_cast<session_id_t>(
                std::stoull(id_text)
            );

            if (!engine_.CloseSession(id)) {
                return ErrorResponse(404, "Session not found");
            }

            return {
                200,
                "{\"message\":\"Session closed\",\"session_id\":" +
                    std::to_string(id) + "}"
            };
        }

        if (method == "POST" && path == "/api/sql") {
            const auto request = JsonObjectParser(body).Parse();

            const auto session_id = static_cast<session_id_t>(
                GetUnsigned(request, "session_id")
            );

            const std::string sql = GetString(request, "sql");

            const auto results = engine_.Execute(session_id, sql);

            std::ostringstream json;
            json << "{\"results\":[";

            for (std::size_t i = 0; i < results.size(); ++i) {
                if (i != 0) {
                    json << ',';
                }

                const auto& result = results[i];

                json << "{\"message\":"
                     << JsonString(result.message)
                     << ",\"rows_affected\":"
                     << result.rows_affected
                     << ",\"columns\":[";

                for (std::size_t c = 0;
                     c < result.columns.size(); ++c) {
                    if (c != 0) {
                        json << ',';
                    }

                    json << JsonString(result.columns[c]);
                }

                json << "],\"rows\":[";

                for (std::size_t r = 0;
                     r < result.rows.size(); ++r) {
                    if (r != 0) {
                        json << ',';
                    }

                    json << '[';

                    for (std::size_t c = 0;
                         c < result.rows[r].size(); ++c) {
                        if (c != 0) {
                            json << ',';
                        }

                        json << JsonValue(result.rows[r][c]);
                    }

                    json << ']';
                }

                json << "]}";
            }

            json << "]}";

            return {200, json.str()};
        }

        if (method == "POST" && path == "/api/experiments/scheduler/start") {
            SchedulerExperiment* experiment = nullptr;
            {
                std::lock_guard<std::mutex> lock(metrics_mutex_);
                experiment = scheduler_experiment_;
            }
            if (!experiment) {
                return ErrorResponse(503, "Scheduler experiment is not configured");
            }
            const auto request = JsonObjectParser(body).Parse();
            const auto threads = GetUnsigned(request, "threads");
            const auto duration_ms = GetUnsigned(request, "duration_ms");
            const auto job_id = experiment->Start(
                static_cast<std::size_t>(threads), duration_ms
            );
            return {202, "{\"job_id\":" + std::to_string(job_id) +
                ",\"status\":\"running\"}"};
        }

        if (method == "GET" && path == "/api/experiments/scheduler/status") {
            SchedulerExperiment* experiment = nullptr;
            {
                std::lock_guard<std::mutex> lock(metrics_mutex_);
                experiment = scheduler_experiment_;
            }
            if (!experiment) {
                return ErrorResponse(503, "Scheduler experiment is not configured");
            }
            return {200, experiment->ToJson()};
        }

        if (method == "GET" && path == "/api/metrics") {
            MetricsProvider provider;
            {
                std::lock_guard<std::mutex> lock(metrics_mutex_);
                provider = metrics_provider_;
            }
            if (!provider) {
                return ErrorResponse(503, "Runtime metrics provider is not configured");
            }
            return {200, provider()};
        }

        if (method == "OPTIONS") {
            return {204, ""};
        }

        return ErrorResponse(404, "API endpoint not found");

    } catch (const std::exception& error) {
        return ErrorResponse(400, error.what());
    }
}

} // namespace minidb