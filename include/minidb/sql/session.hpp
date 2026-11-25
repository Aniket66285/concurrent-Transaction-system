
#pragma once

#include "minidb/concurrency/transaction.hpp"

#include <cstdint>
#include <map>
#include <memory>
#include <mutex>
#include <string>
#include <vector>

namespace minidb {

using session_id_t = std::uint64_t;

struct SessionSnapshot {
    session_id_t session_id{0};
    bool has_transaction{false};
    bool session_busy{false};
    txn_id_t transaction_id{0};
    std::string transaction_state{"IDLE"};
};

class SqlEngine;

class Session {
public:

    explicit Session(session_id_t id);

    Session(const Session&) = delete;
    Session& operator=(const Session&) = delete;

    session_id_t GetId() const noexcept;

    bool IsClosed() const;

    bool HasActiveTransaction() const;

    SessionSnapshot GetSnapshot() const;

private:

    friend class SqlEngine;

    const session_id_t id_;

    mutable std::mutex mutex_;

    std::shared_ptr<Transaction> transaction_;

    bool closed_{false};
};


class SessionManager {
public:

    SessionManager() = default;

    SessionManager(const SessionManager&) = delete;
    SessionManager& operator=(const SessionManager&) = delete;

    // Create an independent client session.
    std::shared_ptr<Session> CreateSession();

    // Find an existing session.
    std::shared_ptr<Session> GetSession(
        session_id_t session_id
    ) const;

    // Remove a session from the registry.
    bool RemoveSession(session_id_t session_id);

    std::size_t GetSessionCount() const;

    std::vector<session_id_t> GetSessionIds() const;

    std::vector<SessionSnapshot> GetSnapshots() const;

private:

    mutable std::mutex mutex_;

    session_id_t next_session_id_{1};

    std::map<session_id_t, std::shared_ptr<Session>> sessions_;
};

} // namespace minidb
