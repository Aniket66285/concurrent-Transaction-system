#include "wfg.h"

bool findCycle(int start, vector<Txn>& txns, LockManager& lm, vector<int>& cycle) {
    cycle.clear();
    cycle.push_back(start);
    int cur = start;

    while (true) {
        if (txns[cur].state != BLOCKED) return false;       // chain ends: no deadlock
        int next = lm.ownerOf(txns[cur].wantedAccount);     // follow the edge
        if (next == -1) return false;
        if (next == start) return true;                     // back to start = CYCLE

        for (size_t i = 0; i < cycle.size(); i++)           // loop not through start
            if (cycle[i] == next) return false;
        cycle.push_back(next);
        cur = next;
    }
}

int chooseVictim(const vector<int>& cycle, const vector<Txn>& txns) {
    int victim = cycle[0];
    for (size_t i = 1; i < cycle.size(); i++)
        if (txns[cycle[i]].id > txns[victim].id) victim = cycle[i];
    return victim;
}
