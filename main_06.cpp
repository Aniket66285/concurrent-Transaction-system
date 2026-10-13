// MODULE 6 - BANKER'S ALGORITHM (deadlock avoidance), run on its own.
// A free lock is granted only if the new state is SAFE.
#include <cstdio>
#include "test_helper.h"
#include "banker.h"

static Txn makeTxn(int id, int firstAccount, int secondAccount) {
    Txn t;
    t.id = id; t.arrival = 0; t.cpu = 1; t.io = 0;
    t.held.clear();
    t.claim.clear();
    t.claim.push_back(firstAccount);       // accounts this txn MAY need
    t.claim.push_back(secondAccount);
    return t;
}

int main() {
    printf("MODULE 6 - Banker's Algorithm\n\n");
    LockManager lm;
    vector<Txn> txns;
    txns.push_back(makeTxn(1, 1, 2));      // T1 may need A and B
    txns.push_back(makeTxn(2, 2, 1));      // T2 may need B and A
    printf("T1 may need A and B.  T2 may need B and A.\n\n");

    printf("Step 1: T1 asks for A (nobody holds anything)\n");
    check("safe -> GRANT", isSafe(0, 1, txns, lm));
    lm.grant(txns[0], 0, 1);

    printf("\nStep 2: T2 asks for B. B is free, but is it safe?\n");
    bool safe = isSafe(1, 2, txns, lm);
    printf("  If T2 got B: T1 would wait for B, T2 would wait for A -> deadlock risk\n");
    check("UNSAFE -> T2 must WAIT (even though B is free)", !safe);

    printf("\nStep 3: T1 asks for B\n");
    check("safe -> GRANT (T1 can finish, then T2 can go)", isSafe(0, 2, txns, lm));
    lm.grant(txns[0], 0, 2);

    printf("\nStep 4: T1 commits and releases A and B, then T2 asks for B\n");
    lm.releaseAll(txns[0]);
    check("safe -> GRANT", isSafe(1, 2, txns, lm));
    return testFinish();
}
