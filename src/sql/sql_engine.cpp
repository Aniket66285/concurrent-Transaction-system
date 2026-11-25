
#include "minidb/sql/sql_engine.hpp"

#include "minidb/common/exception.hpp"
#include "minidb/sql/lexer.hpp"
#include "minidb/sql/parser.hpp"

#include <memory>
#include <mutex>
#include <utility>

namespace minidb {


// ==================================================
// CONSTRUCTOR
// ==================================================

SqlEngine::SqlEngine(
    Catalog& catalog,
    BufferPool& buffer_pool,
    TxnManager& txn_manager
)
    : catalog_(catalog),
      buffer_pool_(buffer_pool),
      txn_manager_(txn_manager),
      executor_(catalog, buffer_pool, txn_manager) {
}


// ==================================================
// OPEN SESSION
// ==================================================

session_id_t SqlEngine::OpenSession() {

    const auto session =
        session_manager_.CreateSession();

    return session->GetId();
}


// ==================================================
// CLOSE SESSION
// ==================================================

bool SqlEngine::CloseSession(session_id_t session_id) {

    const auto session =
        session_manager_.GetSession(session_id);

    if (!session) {
        return false;
    }

    {
        std::lock_guard<std::mutex> lock(session->mutex_);

        if (session->closed_) {
            return false;
        }

        // Disconnecting a client must not leave
        // transactions and locks behind.
        if (session->transaction_ &&
            session->transaction_->IsActive()) {

            executor_.AbortTransaction(
                *session->transaction_
            );
        }

        session->transaction_.reset();

        session->closed_ = true;
    }

    return session_manager_.RemoveSession(session_id);
}


// ==================================================
// EXECUTE SQL
// ==================================================

std::vector<ExecutionResult> SqlEngine::Execute(
    session_id_t session_id,
    const std::string& sql
) {

    const auto session =
        session_manager_.GetSession(session_id);

    if (!session) {
        throw MiniDBException(
            "Unknown SQL session: " +
            std::to_string(session_id)
        );
    }

    // One session processes its statements in order.
    // Different sessions can execute concurrently.
    std::lock_guard<std::mutex> lock(session->mutex_);

    if (session->closed_) {
        throw MiniDBException(
            "SQL session is closed"
        );
    }

    Lexer lexer(sql);

    const std::vector<Token> tokens =
        lexer.Tokenize();

    Parser parser(tokens);

    const std::vector<Statement> statements =
        parser.Parse();

    std::vector<ExecutionResult> results;

    results.reserve(statements.size());

    for (const Statement& statement : statements) {

        results.push_back(
            executor_.Execute(
                statement,
                session->transaction_
            )
        );
    }

    return results;
}


// ==================================================
// SESSION MONITORING
// ==================================================

std::size_t SqlEngine::GetSessionCount() const {
    return session_manager_.GetSessionCount();
}


std::vector<session_id_t> SqlEngine::GetSessionIds() const {
    return session_manager_.GetSessionIds();
}


std::vector<SessionSnapshot> SqlEngine::GetSessionSnapshots() const {
    return session_manager_.GetSnapshots();
}

} // namespace minidb
