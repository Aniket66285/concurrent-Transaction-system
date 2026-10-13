// MODULE 6 - BANKER'S ALGORITHM (deadlock AVOIDANCE)
// Every txn declares up front the accounts it MAY need (t.claim).
// A free lock is granted only if the new state is SAFE.
#ifndef BANKER_H
#define BANKER_H

#include "common.h"
#include "lock_manager.h"

// Would the system still be safe if txn 'who' were given 'account' now?
// Safe = there is an order (safe sequence) in which every txn can finish.
bool isSafe(int who, int account, vector<Txn>& txns, LockManager& lm);

#endif
