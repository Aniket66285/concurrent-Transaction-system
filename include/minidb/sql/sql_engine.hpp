
#pragma once

#include "minidb/catalog/catalog.hpp"
#include "minidb/buffer/buffer_pool.hpp"
#include "minidb/transaction/txn_manager.hpp"

#include "minidb/sql/executor.hpp"
#include "minidb/sql/session.hpp"

#include <cstddef>
#include <string>
#include <vector>

namespace minidb {

class SqlEngine {
public:

    SqlEngine(
        Catalog& catalog,
        BufferPool& buffer_pool,
        TxnManager& txn_manager
    );

    SqlEngine(const SqlEngine&) = delete;
    SqlEngine& operator=(const SqlEngine&) = delete;

    // Create a new independent client session.
    session_id_t OpenSession();

    // Close session.
    // An active transaction is automatically rolled back.
    bool CloseSession(session_id_t session_id);

    // Execute one or more SQL statements in a session.
    std::vector<ExecutionResult> Execute(
        session_id_t session_id,
        const std::string& sql
    );

    std::size_t GetSessionCount() const;

    std::vector<session_id_t> GetSessionIds() const;

    std::vector<SessionSnapshot> GetSessionSnapshots() const;

private:

    Catalog& catalog_;

    BufferPool& buffer_pool_;

    TxnManager& txn_manager_;

    Executor executor_;

    SessionManager session_manager_;
};

} // namespace minidb
