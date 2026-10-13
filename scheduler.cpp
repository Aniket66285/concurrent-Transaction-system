#include "scheduler.h"

const char* algoName(Algo a) {
    return a == FCFS ? "FCFS" : "SJF";
}

int remainingBurst(const Txn& t) {
    return ((int)t.ops.size() - t.nextOp) * t.cpu - t.progress;
}

// AGING (SJF): the longer a txn waits (now - lastRun), the smaller its key gets,
// so a long job cannot be starved forever by a stream of short jobs.
int agingKey(const Txn& t, int now) {
    return remainingBurst(t) * AGE_STEP - (now - t.lastRun);
}

int pickNext(vector<Txn>& txns, vector<int>& ready, Algo a, int now) {
    int best = 0;                                   // FCFS: front of the queue
    if (a == SJF) {
        for (size_t k = 1; k < ready.size(); k++)
            if (agingKey(txns[ready[k]], now) < agingKey(txns[ready[best]], now))
                best = (int)k;
    }
    int chosen = ready[best];
    ready.erase(ready.begin() + best);
    return chosen;
}
