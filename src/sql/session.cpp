
#include "minidb/sql/session.hpp"

#include "minidb/common/exception.hpp"

#include <limits>

namespace minidb {

// ==================================================
// SESSION
// ==================================================

Session::Session(session_id_t id)
    : id_(id) {
}


session_id_t Session::GetId() const noexcept {
    return id_;
}


bool Session::IsClosed() const {

    std::lock_guard<std::mutex> lock(mutex_);

    return closed_;
}


bool Session::HasActiveTransaction() const {

    std::lock_guard<std::mutex> lock(mutex_);

    return transaction_ != nullptr &&
           transaction_->IsActive();
}


SessionSnapshot Session::GetSnapshot() const {
    // Observability must never block behind a query that is waiting on a row lock.
    // In that case report BUSY without reading the protected transaction pointer.
    std::unique_lock<std::mutex> lock(mutex_, std::try_to_lock);

    SessionSnapshot snapshot;
    snapshot.session_id = id_;

    if (!lock.owns_lock()) {
        snapshot.session_busy = true;
        snapshot.transaction_state = "BUSY / EXECUTING";
        return snapshot;
    }

    if (!transaction_) {
        snapshot.transaction_state = closed_ ? "CLOSED" : "IDLE";
        return snapshot;
    }

    snapshot.transaction_id = transaction_->GetId();
    const auto state = transaction_->GetState();

    switch (state) {
        case TransactionState::ACTIVE:
            snapshot.has_transaction = true;
            snapshot.transaction_state = "ACTIVE";
            break;
        case TransactionState::COMMITTED:
            snapshot.transaction_state = "COMMITTED";
            break;
        case TransactionState::ABORTED:
            snapshot.transaction_state = "ABORTED";
            break;
    }

    return snapshot;
}


// ==================================================
// SESSION MANAGER
// ==================================================

std::shared_ptr<Session> SessionManager::CreateSession() {

    std::lock_guard<std::mutex> lock(mutex_);

    if (next_session_id_ == 0 ||
        next_session_id_ ==
            std::numeric_limits<session_id_t>::max()) {

        throw MiniDBException(
            "Session ID limit reached"
        );
    }

    const session_id_t id = next_session_id_++;

    auto session = std::make_shared<Session>(id);

    sessions_.emplace(id, session);

    return session;
}


std::shared_ptr<Session> SessionManager::GetSession(
    session_id_t session_id
) const {

    std::lock_guard<std::mutex> lock(mutex_);

    const auto iterator = sessions_.find(session_id);

    if (iterator == sessions_.end()) {
        return nullptr;
    }

    return iterator->second;
}


bool SessionManager::RemoveSession(session_id_t session_id) {

    std::lock_guard<std::mutex> lock(mutex_);

    return sessions_.erase(session_id) != 0;
}


std::size_t SessionManager::GetSessionCount() const {

    std::lock_guard<std::mutex> lock(mutex_);

    return sessions_.size();
}


std::vector<session_id_t> SessionManager::GetSessionIds() const {

    std::lock_guard<std::mutex> lock(mutex_);

    std::vector<session_id_t> ids;

    ids.reserve(sessions_.size());

    for (const auto& entry : sessions_) {
        ids.push_back(entry.first);
    }

    return ids;
}


std::vector<SessionSnapshot> SessionManager::GetSnapshots() const {
    std::lock_guard<std::mutex> lock(mutex_);

    std::vector<SessionSnapshot> snapshots;
    snapshots.reserve(sessions_.size());

    for (const auto& entry : sessions_) {
        snapshots.push_back(entry.second->GetSnapshot());
    }

    return snapshots;
}

} // namespace minidb
