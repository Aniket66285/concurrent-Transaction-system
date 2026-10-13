// MODULE 5 - WAIT-FOR GRAPH (deadlock DETECTION)
// Edge  Ti -> Tj  means: Ti is blocked on an account that Tj owns.
// Locks are granted freely; when someone blocks we look for a cycle.
#ifndef WFG_H
#define WFG_H

#include "common.h"
#include "lock_manager.h"

// Follow the edges from 'start'. If we come back to start -> cycle found.
// 'cycle' is filled with the txn indexes on the cycle.
bool findCycle(int start, vector<Txn>& txns, LockManager& lm, vector<int>& cycle);

// Victim = the youngest transaction in the cycle (highest id).
int chooseVictim(const vector<int>& cycle, const vector<Txn>& txns);

#endif
