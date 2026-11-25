# MiniDB OS Lab — CPU Scheduling Algorithms

The dashboard contains two separate learning/measurement areas:

1. **Textbook CPU scheduling simulator** — deterministic simulation over the process table supplied by the learner.
2. **Linux kernel scheduling measurement** — actual MiniDB worker threads are measured through Linux procfs and schedstat counters.

These are intentionally separate. The browser simulator does not change the Linux kernel scheduler, and the kernel experiment does not pretend Linux ran FCFS/SJF/RR.

## Process input fields

- **PID**: unique process label (e.g. P1).
- **AT (Arrival Time)**: time at which a process enters the ready queue.
- **BT (Burst Time)**: total CPU time the process needs.
- **Priority**: smaller number means higher priority in this simulator.
- **Queue**: fixed queue number 0, 1 or 2 for the MLQ variant; queue 0 is highest priority.

All times are integer time units. Burst must be greater than zero. The simulator accepts up to 12 processes and caps total burst at 50,000 time units to keep the browser responsive.

## Algorithms included

### 1. FCFS — First Come First Served

Non-preemptive. The process with the earliest arrival is selected; it runs until its burst finishes. It is simple and fair by arrival order, but a long process at the front can make short jobs wait (convoy effect).

### 2. SJF — Shortest Job First

Non-preemptive. Among the processes that have already arrived, choose the smallest original burst time. It often gives low average waiting time when burst lengths are known, but long jobs can starve.

### 3. SRTF — Shortest Remaining Time First

Preemptive form of SJF. At each time unit, run the ready process with the smallest remaining burst. A newly arrived shorter process can preempt the current process.

### 4. Priority Scheduling — Non-preemptive

Among ready jobs, choose the highest priority. In this simulator, a smaller priority number is higher. Once selected, the process runs to completion. Starvation can affect low-priority jobs; aging is a common mitigation in real designs.

### 5. Priority Scheduling — Preemptive

A newly arrived process with a higher priority can preempt the running process. Equal-priority ties favour the current process to avoid unnecessary switching.

### 6. Round Robin (RR)

Preemptive time-sharing policy. Each ready process receives up to the configured time quantum. If it is not complete, it is placed at the end of the ready queue. A very large quantum behaves more like FCFS; an extremely small quantum creates more switching overhead in a real OS (the simulator does not model a real context-switch cost).

### 7. HRRN — Highest Response Ratio Next

Non-preemptive. Select the ready process with the largest response ratio:

`Response Ratio = (Waiting Time + Burst Time) / Burst Time`

Waiting jobs gain ratio as they wait, which can reduce starvation compared with SJF.

### 8. MLQ — Multilevel Queue Scheduling

Fixed queue assignment is supplied in the Queue column. The included teaching configuration is:

- Queue 0: highest precedence, Round Robin with quantum 2.
- Queue 1: middle precedence, Round Robin with quantum 4.
- Queue 2: lowest precedence, FCFS.

A ready process in a higher queue preempts a process running in a lower queue. Processes do not move between queues in MLQ.

### 9. MLFQ — Multilevel Feedback Queue

All new processes begin in Queue 0. The included teaching variant uses three levels with quanta 2, 4 and 8. A process that consumes its full quantum is demoted to the next lower queue (the lowest queue remains at that level). Higher queues can preempt lower queues. A global priority boost every 30 time units promotes ready/running work back to Queue 0 to reduce starvation. This is one explicit MLFQ variant; operating systems can use different queue/boost policies.

## Metrics and formulas

- **CT — Completion Time**: time at which process finishes.
- **TAT — Turnaround Time**: `CT - AT`.
- **WT — Waiting Time**: `TAT - BT`.
- **RT — Response Time**: `First CPU Start - AT`.
- **Average WT/TAT/RT**: sum of the respective per-process value divided by process count.
- **CPU Utilization**: `Total Busy CPU Time / Elapsed Time from t=0 to final completion × 100`.
- **Throughput**: number of completed processes divided by elapsed time.
- **Process Switches**: transitions between different process IDs in the simulated Gantt sequence. This is not a Linux context-switch counter.

The Gantt chart includes IDLE intervals. Context-switch execution cost is not deducted from simulated CPU time.

## Linux scheduler measurement (separate panel below)

The Linux panel submits CPU-bound tasks to MiniDB's real worker pool and records worker Linux TIDs. It compares before/after kernel counters including CPU time, last CPU, scheduler policy/priority, voluntary/involuntary context switches and schedstat runtime/run-queue/timeslice values when available.

Typical Linux policies to know:

- `SCHED_OTHER`: normal time-sharing policy for ordinary application threads.
- `SCHED_FIFO`: real-time fixed-priority scheduling; permission/capability restrictions apply.
- `SCHED_RR`: real-time round-robin among threads at the same real-time priority; permission/capability restrictions apply.

The MiniDB experiment measures the current policy of its workers; it does not switch the worker pool to real-time policies. The textbook simulator and actual Linux scheduler experiment answer different questions.
