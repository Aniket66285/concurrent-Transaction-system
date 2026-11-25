# Operating Systems Concepts Used in MiniDB

This guide explains the OS ideas visible in the MiniDB dashboard.

## Process vs thread

A process is a running program with its own virtual address space and process-level resources. A thread is an execution path inside a process. Threads in one process share the process address space and files, while each thread has its own stack, register state and Linux thread ID (TID).

MiniDB is one server process. It starts a small fixed WorkerPool with three C++ worker threads. The server accepts HTTP client connections and submits request work to those workers.

## PID vs TID

- PID identifies the process.
- TID identifies an individual Linux task/thread.
- In Linux, the process leader's TID commonly equals its PID; other threads have their own TIDs.

The OS Monitor reports worker TIDs so the user can correlate MiniDB worker threads with Linux `/proc` data.

## CPU burst and scheduling

A CPU burst is the amount of CPU execution a process needs before it blocks or finishes. The ready queue contains processes that can run but are waiting for a CPU.

The textbook simulator uses integer time units, process arrival time (AT), CPU burst time (BT), priority and (for MLQ) a queue number. It produces a Gantt chart and these metrics:

- CT = completion time.
- TAT = CT − AT.
- WT = TAT − BT.
- RT = first CPU start − AT.
- CPU utilization = busy CPU time / elapsed time from t=0 to final completion × 100.
- Throughput = completed processes / elapsed time.

### Algorithms in the simulator

- FCFS: non-preemptive; earliest arrival first.
- SJF: non-preemptive; smallest burst among arrived processes.
- SRTF: preemptive SJF; smallest remaining burst wins.
- Priority non-preemptive: highest-priority ready process runs to completion.
- Priority preemptive: a newly arrived higher-priority process can preempt the current process.
- Round Robin: each ready process receives up to a fixed time quantum and returns to the queue if unfinished.
- HRRN: non-preemptive; maximizes `(waiting + burst) / burst`.
- MLQ: fixed queue assignment. MiniDB's teaching variant uses Q0 = RR-2, Q1 = RR-4, Q2 = FCFS; higher queues preempt lower queues.
- MLFQ: processes start in Q0; quanta are 2/4/8; consuming a full quantum demotes a process; a global priority boost every 30 time units reduces starvation.

Smaller numeric priority means higher priority. In MLQ, queue 0 is highest. MLQ queue assignment is fixed, while MLFQ moves processes between queues.

### Important: simulator vs real Linux

FCFS/SJF/SRTF/Priority/RR/HRRN/MLQ/MLFQ results are deterministic teaching simulations. They do not cause the Linux kernel to use those algorithms for MiniDB worker threads.

The Linux experiment instead submits real CPU-bound tasks to actual MiniDB worker threads and measures before/after kernel counters such as CPU time, last CPU, scheduling policy/priority, schedstat runtime/run-queue/timeslice values and context switches. The observed policy is supplied by the host Linux system (commonly `SCHED_OTHER` for ordinary application threads).

## Preemptive vs non-preemptive

- Non-preemptive: after selection, a process continues until its CPU burst completes or it blocks.
- Preemptive: the scheduler may take the CPU away from a running process and choose another ready process.

Preemption can improve responsiveness but introduces more scheduling decisions and potential context-switch overhead.

## Context switch

A context switch changes the CPU from executing one thread to another. The kernel saves the old thread's execution state and restores the new thread's state. Linux voluntary and involuntary context-switch counters are real kernel counters; the simulator's process-switch count is only a schedule-transition count and is not the same metric.

## Synchronization primitives

- Mutex: protects a shared data structure so only one thread modifies it at a time.
- Semaphore: maintains a counter and lets producers/consumers wait for available capacity/items.
- Condition variable: allows a thread to sleep until a condition may have changed.

MiniDB's bounded request queue uses two POSIX semaphores (available slots and available items) plus a mutex protecting the queue container.

## Producer-consumer queue

The HTTP accept loop acts as producer. It wraps each accepted connection in a task and pushes it to the bounded queue. A WorkerPool thread acts as consumer: it pops a task, executes it, and then requests another task. The configured queue has 8 waiting task slots; the WorkerPool has 3 long-lived threads.

## CPU scheduling demo checklist

1. Run `./scripts/run_server.sh`.
2. Run `./scripts/run_dashboard.sh` in another terminal.
3. Open the OS Scheduling Lab.
4. Load the sample process set and run each textbook algorithm.
5. Compare all algorithms on the same inputs.
6. Scroll down and start the Linux CPU-bound experiment to inspect actual WorkerPool TIDs and kernel counters.
7. Explain why the Gantt chart is a simulation while the Linux panel is measured from real worker threads.
