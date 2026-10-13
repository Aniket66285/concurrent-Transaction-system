// MODULE 3 - SCHEDULER + AGING
// Decides which ready transaction gets the CPU next.
// Only two algorithms:  FCFS (first come first served)  and  SJF (shortest job first)
#ifndef SCHEDULER_H
#define SCHEDULER_H

#include "common.h"

enum Algo { FCFS, SJF };

const int AGE_STEP = 10;     // bigger = aging helps less

const char* algoName(Algo a);
int  remainingBurst(const Txn& t);                         // CPU ticks still needed
int  agingKey(const Txn& t, int now);                      // SJF: smaller = runs first
int  pickNext(vector<Txn>& txns, vector<int>& ready, Algo a, int now);  // removes it from ready

#endif
