
#include "minidb/concurrency/lock_manager.hpp"

#include "minidb/common/exception.hpp"
#include "minidb/concurrency/transaction.hpp"

#include <algorithm>
#include <utility>

namespace minidb {


// --------------------------------------------------
// Constructor
// --------------------------------------------------

LockManager::LockManager()
    : deadlock_detector_(wait_for_graph_) {
}


// --------------------------------------------------
// LockShared
// --------------------------------------------------

bool LockManager::LockShared(
    Transaction& transaction,
    const RID& rid
) {

    return AcquireLock(
        transaction,
        rid,
        LockMode::SHARED
    );
}


// --------------------------------------------------
// LockExclusive
// --------------------------------------------------

bool LockManager::LockExclusive(
    Transaction& transaction,
    const RID& rid
) {

    return AcquireLock(
        transaction,
        rid,
        LockMode::EXCLUSIVE
    );
}


// --------------------------------------------------
// HasOutstandingWaitRequestLocked
// --------------------------------------------------

bool LockManager::HasOutstandingWaitRequestLocked(
    txn_id_t transaction_id
) const {

    for (const auto& resource : lock_table_) {

        for (const WaitRequest& request : resource.second.waiters) {

            if (request.transaction_id == transaction_id) {
                return true;
            }
        }
    }

    return false;
}


// --------------------------------------------------
// GetBlockersLocked
// --------------------------------------------------

std::set<txn_id_t> LockManager::GetBlockersLocked(
    const ResourceLockState& state,
    txn_id_t transaction_id,
    LockMode mode
) const {

    std::set<txn_id_t> blockers;


    // An exclusive holder blocks all other transactions.
    if (state.exclusive_holder != 0 &&
        state.exclusive_holder != transaction_id) {

        blockers.insert(state.exclusive_holder);
    }


    // An exclusive request must wait for other shared holders.
    if (mode == LockMode::EXCLUSIVE) {

        for (txn_id_t holder : state.shared_holders) {

            if (holder != transaction_id) {
                blockers.insert(holder);
            }
        }
    }


    // Respect FIFO ordering.
    // Earlier incompatible requests are also blockers.
    bool foundCurrentRequest = false;

    for (const WaitRequest& request : state.waiters) {

        if (request.transaction_id == transaction_id) {
            foundCurrentRequest = true;
            break;
        }

        if (request.transaction_id == transaction_id) {
            continue;
        }

        const bool conflicts =
            mode == LockMode::EXCLUSIVE ||
            request.mode == LockMode::EXCLUSIVE;

        if (conflicts) {
            blockers.insert(request.transaction_id);
        }
    }

    if (!foundCurrentRequest) {
        throw TransactionException(
            "Lock request is missing from waiting queue"
        );
    }

    return blockers;
}


// --------------------------------------------------
// UpdateWaitEdgesLocked
// --------------------------------------------------

void LockManager::UpdateWaitEdgesLocked(
    txn_id_t transaction_id,
    const std::set<txn_id_t>& blockers
) {

    auto iterator = wait_edges_.find(transaction_id);

    if (iterator != wait_edges_.end()) {

        // Remove dependencies that no longer exist.
        auto& previous = iterator->second;

        for (auto edge = previous.begin();
             edge != previous.end();) {

            if (blockers.find(*edge) == blockers.end()) {

                wait_for_graph_.RemoveEdge(
                    transaction_id,
                    *edge
                );

                edge = previous.erase(edge);
            }
            else {
                ++edge;
            }
        }
    }

    // Add newly discovered dependencies.
    auto& current = wait_edges_[transaction_id];

    for (txn_id_t blocker : blockers) {

        if (current.insert(blocker).second) {

            wait_for_graph_.AddEdge(
                transaction_id,
                blocker
            );
        }
    }

    if (current.empty()) {
        wait_edges_.erase(transaction_id);
    }
}


// --------------------------------------------------
// ClearOutgoingWaitEdgesLocked
// --------------------------------------------------

void LockManager::ClearOutgoingWaitEdgesLocked(
    txn_id_t transaction_id
) {

    const auto iterator =
        wait_edges_.find(transaction_id);

    if (iterator == wait_edges_.end()) {
        return;
    }

    for (txn_id_t blocker : iterator->second) {

        wait_for_graph_.RemoveEdge(
            transaction_id,
            blocker
        );
    }

    wait_edges_.erase(iterator);
}


// --------------------------------------------------
// RemoveWaitRequestLocked
// --------------------------------------------------

void LockManager::RemoveWaitRequestLocked(
    txn_id_t transaction_id
) {

    for (auto& resource : lock_table_) {

        auto& waiters = resource.second.waiters;

        waiters.erase(
            std::remove_if(
                waiters.begin(),
                waiters.end(),
                [&](const WaitRequest& request) {

                    return request.transaction_id ==
                           transaction_id;
                }
            ),
            waiters.end()
        );
    }

    CleanupEmptyResourcesLocked();
}


// --------------------------------------------------
// CleanupEmptyResourcesLocked
// --------------------------------------------------

void LockManager::CleanupEmptyResourcesLocked() {

    for (auto iterator = lock_table_.begin();
         iterator != lock_table_.end();) {

        const ResourceLockState& state = iterator->second;

        const bool noSharedHolders =
            state.shared_holders.empty();

        const bool noExclusiveHolder =
            state.exclusive_holder == 0;

        const bool noWaiters =
            state.waiters.empty();

        if (noSharedHolders &&
            noExclusiveHolder &&
            noWaiters) {

            iterator = lock_table_.erase(iterator);
        }
        else {
            ++iterator;
        }
    }
}


// --------------------------------------------------
// MarkVictimLocked
// --------------------------------------------------

void LockManager::MarkVictimLocked(
    txn_id_t victim_id
) {

    const bool newlyMarked =
        abort_requested_.insert(victim_id).second;

    if (!newlyMarked) {
        return;
    }

    // Cancel the victim's waiting request.
    RemoveWaitRequestLocked(victim_id);

    // The victim is no longer waiting for another transaction.
    ClearOutgoingWaitEdgesLocked(victim_id);

    // IMPORTANT:
    // Do not release the victim's existing locks here.
    // Its caller must complete rollback first.
    condition_.notify_all();
}


// --------------------------------------------------
// AcquireLock
// --------------------------------------------------

bool LockManager::AcquireLock(
    Transaction& transaction,
    const RID& rid,
    LockMode mode
) {

    transaction.EnsureActive();

    const txn_id_t transactionId =
        transaction.GetId();

    const ResourceId resourceId{
        rid.page_id,
        rid.slot_id
    };

    std::unique_lock<std::mutex> lock(mutex_);


    if (abort_requested_.count(transactionId) != 0) {
        throw TransactionException(
            "Transaction has been selected as a deadlock victim"
        );
    }


    // Reentrant lock acquisition.
    auto existing = lock_table_.find(resourceId);

    if (existing != lock_table_.end()) {

        const ResourceLockState& state = existing->second;

        if (state.exclusive_holder == transactionId) {
            return true;
        }

        if (mode == LockMode::SHARED &&
            state.shared_holders.count(transactionId) != 0) {

            return true;
        }
    }


    // A transaction is expected to issue one outstanding
    // lock request at a time.
    if (HasOutstandingWaitRequestLocked(transactionId)) {

        throw TransactionException(
            "Transaction already has an outstanding lock request"
        );
    }


    // Insert the request into the FIFO queue.
    ResourceLockState& initialState =
        lock_table_[resourceId];

    initialState.waiters.push_back({
        transactionId,
        mode
    });


    while (true) {

        // A different request may have selected this transaction
        // as the deadlock victim while this thread was waiting.
        if (abort_requested_.count(transactionId) != 0) {

            RemoveWaitRequestLocked(transactionId);

            ClearOutgoingWaitEdgesLocked(transactionId);

            condition_.notify_all();

            throw TransactionException(
                "Transaction aborted as a deadlock victim"
            );
        }


        auto iterator = lock_table_.find(resourceId);

        if (iterator == lock_table_.end()) {
            throw TransactionException(
                "Lock resource disappeared while waiting"
            );
        }


        // Find current blockers.
        const std::set<txn_id_t> blockers =
            GetBlockersLocked(
                iterator->second,
                transactionId,
                mode
            );


        // No blockers: grant the lock.
        if (blockers.empty()) {

            auto& state = iterator->second;

            auto requestIterator =
                std::find_if(
                    state.waiters.begin(),
                    state.waiters.end(),
                    [&](const WaitRequest& request) {

                        return request.transaction_id ==
                               transactionId;
                    }
                );

            if (requestIterator == state.waiters.end()) {
                throw TransactionException(
                    "Waiting request disappeared before lock grant"
                );
            }

            state.waiters.erase(requestIterator);


            if (mode == LockMode::EXCLUSIVE) {

                // Upgrade: remove this transaction's shared lock.
                state.shared_holders.erase(transactionId);

                state.exclusive_holder = transactionId;
            }
            else {

                state.shared_holders.insert(transactionId);
            }


            // Track the resource held by this transaction.
            held_locks_[transactionId].insert(resourceId);

            // This transaction is no longer waiting.
            ClearOutgoingWaitEdgesLocked(transactionId);

            condition_.notify_all();

            return true;
        }


        // Update the wait-for graph.
        UpdateWaitEdgesLocked(
            transactionId,
            blockers
        );


        // Detect a cycle.
        const auto deadlock =
            deadlock_detector_.DetectDeadlock();

        if (deadlock.has_value()) {

            const txn_id_t victim =
                deadlock->victim_txn_id;

            MarkVictimLocked(victim);


            if (victim == transactionId) {

                throw TransactionException(
                    "This transaction is the selected deadlock victim"
                );
            }

            // The victim is another waiting transaction.
            // Recalculate blockers before sleeping.
            continue;
        }


        // Wait until some lock is released or a victim is marked.
        condition_.wait(lock);
    }
}


// --------------------------------------------------
// ReleaseAll
// --------------------------------------------------

void LockManager::ReleaseAll(
    const Transaction& transaction
) {

    if (transaction.GetState() == TransactionState::ACTIVE) {

        throw TransactionException(
            "Cannot release Strict 2PL locks before "
            "transaction COMMIT or ABORT"
        );
    }

    const txn_id_t transactionId =
        transaction.GetId();

    std::lock_guard<std::mutex> lock(mutex_);


    // Remove any remaining queued requests.
    RemoveWaitRequestLocked(transactionId);

    // Remove outgoing wait relationships.
    ClearOutgoingWaitEdgesLocked(transactionId);

    // Remove incoming and outgoing edges from the graph.
    wait_for_graph_.RemoveTransaction(transactionId);

    wait_edges_.erase(transactionId);

    for (auto& entry : wait_edges_) {
        entry.second.erase(transactionId);
    }


    // Release all shared and exclusive locks.
    for (auto& entry : lock_table_) {

        auto& state = entry.second;

        state.shared_holders.erase(transactionId);

        if (state.exclusive_holder == transactionId) {
            state.exclusive_holder = 0;
        }
    }


    held_locks_.erase(transactionId);

    abort_requested_.erase(transactionId);

    CleanupEmptyResourcesLocked();

    // Wake all waiting transactions.
    condition_.notify_all();
}


// --------------------------------------------------
// IsWaiting
// --------------------------------------------------

bool LockManager::IsWaiting(
    txn_id_t transaction_id
) const {

    std::lock_guard<std::mutex> lock(mutex_);

    for (const auto& resource : lock_table_) {

        for (const WaitRequest& request : resource.second.waiters) {

            if (request.transaction_id == transaction_id) {
                return true;
            }
        }
    }

    return false;
}


// --------------------------------------------------
// IsAbortRequested
// --------------------------------------------------

bool LockManager::IsAbortRequested(
    txn_id_t transaction_id
) const {

    std::lock_guard<std::mutex> lock(mutex_);

    return abort_requested_.count(transaction_id) != 0;
}


// --------------------------------------------------
// GetHeldLockCount
// --------------------------------------------------

std::size_t LockManager::GetHeldLockCount(
    txn_id_t transaction_id
) const {

    std::lock_guard<std::mutex> lock(mutex_);

    const auto iterator =
        held_locks_.find(transaction_id);

    if (iterator == held_locks_.end()) {
        return 0;
    }

    return iterator->second.size();
}


// --------------------------------------------------
// GetWaitingCount
// --------------------------------------------------

std::size_t LockManager::GetWaitingCount() const {

    std::lock_guard<std::mutex> lock(mutex_);

    std::size_t count = 0;

    for (const auto& resource : lock_table_) {
        count += resource.second.waiters.size();
    }

    return count;
}


// --------------------------------------------------
// GetWaitForEdges
// --------------------------------------------------

std::vector<std::pair<txn_id_t, txn_id_t>>
LockManager::GetWaitForEdges() const {

    std::lock_guard<std::mutex> lock(mutex_);

    return wait_for_graph_.GetEdges();
}


std::vector<LockManager::LockResourceSnapshot>
LockManager::GetLockTableSnapshot() const {
    std::lock_guard<std::mutex> lock(mutex_);
    std::vector<LockResourceSnapshot> result;
    result.reserve(lock_table_.size());

    for (const auto& entry : lock_table_) {
        LockResourceSnapshot snapshot;
        snapshot.page_id = entry.first.first;
        snapshot.slot_id = entry.first.second;
        snapshot.exclusive_holder = entry.second.exclusive_holder;
        snapshot.shared_holders.assign(
            entry.second.shared_holders.begin(),
            entry.second.shared_holders.end()
        );
        snapshot.waiters.reserve(entry.second.waiters.size());
        for (const auto& waiter : entry.second.waiters) {
            snapshot.waiters.push_back({waiter.transaction_id, waiter.mode});
        }
        result.push_back(std::move(snapshot));
    }
    return result;
}


} // namespace minidb
