// MODULE 3 - SCHEDULER + AGING (FCFS and SJF), run on its own.
#include <cstdio>
#include "test_helper.h"
#include "scheduler.h"

static Txn makeTxn(int id, int numOps, int cpu) {
    Txn t;
    t.id = id; t.arrival = 0; t.cpu = cpu; t.io = 0;
    for (int i = 0; i < numOps; i++) { Op o = { 1, 10 }; t.ops.push_back(o); }
    t.state = READY; t.nextOp = 0; t.progress = 0; t.lastRun = 0;
    return t;
}

// take everybody out of the ready queue, return the order of txn ids
static vector<int> order(vector<Txn>& txns, Algo algo, int now) {
    vector<int> ready, ids;
    for (size_t i = 0; i < txns.size(); i++) ready.push_back((int)i);
    while (!ready.empty()) ids.push_back(txns[pickNext(txns, ready, algo, now)].id);
    return ids;
}

static void show(const char* title, const vector<int>& ids) {
    printf("  %s: ", title);
    for (size_t i = 0; i < ids.size(); i++) printf("T%d ", ids[i]);
    printf("\n");
}

int main() {
    printf("MODULE 3 - Scheduler\n\n");
    vector<Txn> txns;
    txns.push_back(makeTxn(1, 4, 2));    // T1: 8 CPU ticks left  (long)
    txns.push_back(makeTxn(2, 1, 1));    // T2: 1 CPU tick left   (short)
    txns.push_back(makeTxn(3, 2, 2));    // T3: 4 CPU ticks left  (medium)
    printf("Ready queue: T1 (8 ticks), T2 (1 tick), T3 (4 ticks)\n\n");

    vector<int> f = order(txns, FCFS, 1);
    vector<int> s = order(txns, SJF, 1);
    show("FCFS order", f);
    show("SJF  order", s);
    check("FCFS keeps arrival order T1 T2 T3", f.size() == 3 && f[0] == 1 && f[1] == 2 && f[2] == 3);
    check("SJF runs the shortest first: T2 T3 T1", s.size() == 3 && s[0] == 2 && s[1] == 3 && s[2] == 1);

    // ---- aging ----
    printf("\nAging: a LONG job (10 ticks) against a SHORT job (1 tick)\n");
    vector<Txn> two;
    two.push_back(makeTxn(1, 5, 2));     // long
    two.push_back(makeTxn(2, 1, 1));     // short
    vector<int> early = order(two, SJF, 1);                 // both just arrived
    show("now = 1   (nobody waited)", early);
    check("short job goes first when nobody has waited", early[0] == 2);

    two[0].lastRun = 0;                  // long job has waited since tick 0
    two[1].lastRun = 99;                 // short job ran a moment ago
    vector<int> late = order(two, SJF, 100);
    show("now = 100 (long job waited 100 ticks)", late);
    check("aging lets the long job go first (no starvation)", late[0] == 1);
    return testFinish();
}
