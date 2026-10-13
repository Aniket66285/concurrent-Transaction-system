#include "banker.h"

bool isSafe(int who, int account, vector<Txn>& txns, LockManager& lm) {
    int n = (int)txns.size();

    // work[a] = true  -> account a is free (available)
    bool work[NUM_ACCOUNTS + 1];
    for (int a = 1; a <= NUM_ACCOUNTS; a++)
        work[a] = lm.isFree(a) && a != account;       // pretend 'account' is given away

    // alloc[i][a] = i holds a,   need[i][a] = i may still ask for a
    bool alloc[64][NUM_ACCOUNTS + 1];
    bool need[64][NUM_ACCOUNTS + 1];
    bool finished[64];

    for (int i = 0; i < n; i++) {
        for (int a = 1; a <= NUM_ACCOUNTS; a++) { alloc[i][a] = false; need[i][a] = false; }
        for (size_t k = 0; k < txns[i].held.size(); k++) alloc[i][txns[i].held[k]] = true;
        if (i == who) alloc[i][account] = true;

        bool holdsSomething = false;
        for (int a = 1; a <= NUM_ACCOUNTS; a++) if (alloc[i][a]) holdsSomething = true;
        finished[i] = !holdsSomething;                 // holds nothing: ignore for now

        for (size_t k = 0; k < txns[i].claim.size(); k++) {
            int a = txns[i].claim[k];
            if (!alloc[i][a]) need[i][a] = true;
        }
    }

    // Look for a safe sequence: pick any txn whose needs are all available,
    // let it finish, take back what it holds, repeat.
    bool progress = true;
    while (progress) {
        progress = false;
        for (int i = 0; i < n; i++) {
            if (finished[i]) continue;
            bool canFinish = true;
            for (int a = 1; a <= NUM_ACCOUNTS; a++)
                if (need[i][a] && !work[a]) canFinish = false;
            if (canFinish) {
                for (int a = 1; a <= NUM_ACCOUNTS; a++) if (alloc[i][a]) work[a] = true;
                finished[i] = true;
                progress = true;
            }
        }
    }
    for (int i = 0; i < n; i++) if (!finished[i]) return false;   // no safe sequence
    return true;
}
