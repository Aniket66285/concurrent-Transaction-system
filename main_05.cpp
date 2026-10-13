// MODULE 5 - WAIT-FOR GRAPH (deadlock detection), run on its own.
// Edge Ti -> Tj means: Ti is blocked on an account that Tj owns.
#include <cstdio>
#include "test_helper.h"
#include "wfg.h"

static Txn makeTxn(int id) {
    Txn t;
    t.id = id; t.arrival = 0; t.cpu = 1; t.io = 0;
    t.state = RUNNING; t.wantedAccount = 0; t.held.clear();
    return t;
}

// txn i locks account a
static void holds(LockManager& lm, vector<Txn>& txns, int i, int a) { lm.grant(txns[i], i, a); }

// txn i is blocked waiting for account a
static void waitsFor(vector<Txn>& txns, int i, int a) { txns[i].state = BLOCKED; txns[i].wantedAccount = a; }

static void printCycle(const vector<int>& cycle, const vector<Txn>& txns) {
    printf("  cycle: ");
    for (size_t k = 0; k < cycle.size(); k++) printf("T%d -> ", txns[cycle[k]].id);
    printf("T%d\n", txns[cycle[0]].id);
}

int main() {
    printf("MODULE 5 - Wait-For Graph\n");
    vector<int> cycle;

    // ---- case 1: waiting, but no cycle ----
    printf("\nCASE 1: T1 holds A, T2 waits for A (T1 is not waiting)\n");
    {
        LockManager lm;
        vector<Txn> txns; txns.push_back(makeTxn(1)); txns.push_back(makeTxn(2));
        holds(lm, txns, 0, 1);
        waitsFor(txns, 1, 1);
        check("no deadlock", !findCycle(1, txns, lm, cycle));
    }

    // ---- case 2: classic two-transaction deadlock ----
    printf("\nCASE 2: T1 holds A wants B, T2 holds B wants A\n");
    {
        LockManager lm;
        vector<Txn> txns; txns.push_back(makeTxn(1)); txns.push_back(makeTxn(2));
        holds(lm, txns, 0, 1);  holds(lm, txns, 1, 2);
        waitsFor(txns, 0, 2);   waitsFor(txns, 1, 1);
        bool found = findCycle(0, txns, lm, cycle);
        check("deadlock found", found);
        if (found) printCycle(cycle, txns);
        check("cycle has 2 transactions", cycle.size() == 2);
        int v = chooseVictim(cycle, txns);
        printf("  victim: T%d (the youngest = highest id)\n", txns[v].id);
        check("victim is T2", txns[v].id == 2);
    }

    // ---- case 3: a cycle of three ----
    printf("\nCASE 3: T1 holds A wants B, T2 holds B wants C, T3 holds C wants A\n");
    {
        LockManager lm;
        vector<Txn> txns;
        txns.push_back(makeTxn(1)); txns.push_back(makeTxn(2)); txns.push_back(makeTxn(3));
        holds(lm, txns, 0, 1); holds(lm, txns, 1, 2); holds(lm, txns, 2, 3);
        waitsFor(txns, 0, 2);  waitsFor(txns, 1, 3);  waitsFor(txns, 2, 1);
        bool found = findCycle(0, txns, lm, cycle);
        check("deadlock found", found);
        if (found) printCycle(cycle, txns);
        check("cycle has 3 transactions", cycle.size() == 3);
        int v = chooseVictim(cycle, txns);
        printf("  victim: T%d\n", txns[v].id);
        check("victim is T3", txns[v].id == 3);
    }
    return testFinish();
}
