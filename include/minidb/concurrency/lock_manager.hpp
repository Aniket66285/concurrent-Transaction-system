
#pragma once

#include "minidb/common/types.hpp"
#include "minidb/concurrency/wait_for_graph.hpp"
#include "minidb/concurrency/deadlock_detector.hpp"

#include <condition_variable>
#include <cstddef>
#include <deque>
#include <map>
#include <mutex>
#include <set>
#include <utility>
#include <vector>

namespace minidb {

class Transaction;

class LockManager {
public:

    struct LockWaiterSnapshot {
        txn_id_t transaction_id{0};
        LockMode mode{LockMode::SHARED};
    };

    struct LockResourceSnapshot {
        page_id_t page_id{0};
        slot_id_t slot_id{0};
        txn_id_t exclusive_holder{0};
        std::vector<txn_id_t> shared_holders;
        std::vector<LockWaiterSnapshot> waiters;
    };

    LockManager();

    LockManager(const LockManager&) = delete;
    LockManager& operator=(const LockManager&) = delete;

    // Acquires a shared/read lock.
    // Multiple transactions can hold shared locks together.
    bool LockShared(Transaction& transaction, const RID& rid);

    // Acquires an exclusive/write lock.
    // Only one transaction can hold an exclusive lock.
    bool LockExclusive(Transaction& transaction, const RID& rid);

    // Releases every lock held by a completed transaction.
    // The transaction must already be COMMITTED or ABORTED.
    void ReleaseAll(const Transaction& transaction);

    // Monitoring methods.
    bool IsWaiting(txn_id_t transaction_id) const;

    bool IsAbortRequested(txn_id_t transaction_id) const;

    std::size_t GetHeldLockCount(txn_id_t transaction_id) const;

    std::size_t GetWaitingCount() const;

    std::vector<std::pair<txn_id_t, txn_id_t>>
    GetWaitForEdges() const;

    std::vector<LockResourceSnapshot> GetLockTableSnapshot() const;

private:

    using ResourceId = std::pair<page_id_t, slot_id_t>;

    struct WaitRequest {

        txn_id_t transaction_id;

        LockMode mode;
    };

    struct ResourceLockState {

        // Transactions holding shared locks.
        std::set<txn_id_t> shared_holders;

        // Zero means no exclusive holder.
        txn_id_t exclusive_holder{0};

        // FIFO waiting queue.
        std::deque<WaitRequest> waiters;
    };

    // Common implementation for S and X locks.
    bool AcquireLock(
        Transaction& transaction,
        const RID& rid,
        LockMode mode
    );

    // Finds transactions blocking a particular request.
    std::set<txn_id_t> GetBlockersLocked(
        const ResourceLockState& state,
        txn_id_t transaction_id,
        LockMode mode
    ) const;

    // Updates graph edges for a waiting transaction.
    void UpdateWaitEdgesLocked(
        txn_id_t transaction_id,
        const std::set<txn_id_t>& blockers
    );

    // Removes outgoing wait-for edges.
    void ClearOutgoingWaitEdgesLocked(
        txn_id_t transaction_id
    );

    // Removes a transaction from all waiting queues.
    void RemoveWaitRequestLocked(
        txn_id_t transaction_id
    );

    // Removes an empty resource entry.
    void CleanupEmptyResourcesLocked();

    // Marks a selected deadlock victim.
    // Existing held locks are deliberately retained
    // until its rollback completes.
    void MarkVictimLocked(txn_id_t victim_id);

    // Checks whether a transaction already has a waiting request.
    bool HasOutstandingWaitRequestLocked(
        txn_id_t transaction_id
    ) const;

    mutable std::mutex mutex_;

    std::condition_variable condition_;

    std::map<ResourceId, ResourceLockState> lock_table_;

    // Locks currently held by each transaction.
    std::map<txn_id_t, std::set<ResourceId>> held_locks_;

    // Blockers currently recorded for each waiting transaction.
    std::map<txn_id_t, std::set<txn_id_t>> wait_edges_;

    // Transactions selected as deadlock victims.
    std::set<txn_id_t> abort_requested_;

    // Graph and detector used for deadlock detection.
    WaitForGraph wait_for_graph_;

    DeadlockDetector deadlock_detector_;
};

} // namespace minidb
