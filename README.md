# MiniDB Systems Lab — C++17 DBMS + Operating Systems Project

MiniDB is an educational database engine written in C++17. It combines a custom page-based storage layer, SQL execution, transaction/lock management, a bounded producer-consumer request queue, a fixed worker pool, an HTTP API, and a browser dashboard for observing database and Linux runtime behaviour.

This is a teaching/research prototype, not a production database. The sections below distinguish implemented features from known limitations.

## What is implemented

- 4096-byte database pages and a file-backed `DiskManager`.
- Slotted pages, record slots/RIDs and a linked-page `TableHeap`.
- Four-frame `BufferPool`, pin/unpin tracking and LRU replacement.
- WAL logging primitives, log records, checkpoints and a simplified recovery manager with crash tests.
- Transactions, shared/exclusive record locks, strict two-phase-locking behaviour, wait-for graph and deadlock detection.
- SQL lexer, recursive-descent parser and executor for the supported SQL subset.
- Independent server-side SQL sessions.
- Linux/POSIX bounded request queue (capacity 8) and three long-lived C++ worker threads.
- HTTP server and JSON API.
- A two-client Concurrent SQL Lab that uses two distinct sessions and sends overlapping HTTP SQL requests.
- A textbook CPU scheduling simulator covering FCFS, SJF, SRTF, Priority (preemptive/non-preemptive), Round Robin, HRRN, Multilevel Queue and Multilevel Feedback Queue. It renders Gantt charts, CT/TAT/WT/RT tables, CPU utilization, throughput and a side-by-side algorithm comparison.
- A separate Linux scheduler experiment that runs CPU-bound work on actual MiniDB WorkerPool threads and reports their Linux TIDs and scheduler/procfs counter deltas.
- Browser dashboard and a terminal SQL client.

## Architecture

```text
Browser Client A ─┐                         ┌─ Session A ─┐
                  ├─ HTTP Server ─ Queue ─ WorkerPool ─ SQL Engine ─ Transactions/Locks
Browser Client B ─┘                   (8)    (3 threads)  └─ Session B ─┘
                                                               │
                                                TableHeap / BufferPool
                                                               │
                                                     DiskManager + WAL

OS Scheduling Lab ─ CPU-bound tasks on the same three WorkerPool threads
                  └─ TIDs, Linux scheduling policy, CPU time, schedstat and context switches
```

## Requirements

- Linux (the OS monitoring uses `/proc` and Linux scheduling interfaces).
- C++17 compiler such as GCC 9+.
- CMake 3.16+.
- POSIX threads/semaphores.
- Python 3 for serving the static dashboard.
- `curl` and Python 3 for the live integration test scripts.
- Node.js 18+ for the CPU scheduling simulator tests and JavaScript syntax checks in `test_telemetry.sh`. It is not required just to serve the static dashboard.

On Linux Mint/Ubuntu, install the basic build tools if needed:

```bash
sudo apt update
sudo apt install build-essential cmake python3 curl
```

## Build and run

From the project root:

```bash
./scripts/build.sh
```

Start the MiniDB backend (Terminal 1):

```bash
./scripts/run_server.sh
```

Start the dashboard (Terminal 2):

```bash
./scripts/run_dashboard.sh
```

Open `http://127.0.0.1:5500` in a browser. The backend listens on port 8080 by default. You can override it with `MINIDB_PORT`, but the dashboard's default API address is `http://127.0.0.1:8080`.

Start a terminal SQL client (Terminal 3):

```bash
./scripts/run_client.sh
```

The client opens its own server-side SQL session. Run two terminal clients in two terminals to get two separate sessions.

## Demonstrate real two-client concurrency

1. Start the backend and open the dashboard.
2. Select **Concurrency Lab** from the sidebar.
3. Click **Create / reconnect Client A & B**. The two panels receive different session IDs.
4. Click **Setup accounts demo table** once. It creates `accounts(id, balance)` with `(1, 1000)`.
5. Run Client A first:

   ```sql
   BEGIN;
   UPDATE accounts SET balance = balance - 100 WHERE id = 1;
   ```

   A's request finishes, but its explicit transaction remains open and holds its record lock.

6. Run Client B:

   ```sql
   BEGIN;
   UPDATE accounts SET balance = balance - 50 WHERE id = 1;
   ```

   B's request should remain in the running/waiting state because it needs a conflicting lock on the same RID.

7. Observe the live Lock Table and Wait-for Graph. A wait-for edge means the waiting transaction depends on a lock held by another transaction.
8. Click **Commit** for Client A. A releases its locks and B should continue.
9. Click **Commit** for Client B. Then run `SELECT * FROM accounts;`. The expected balance is `850`.

Do not put both client transactions into one session or one SQL batch. The point of the lab is that they are independent sessions, handled by separate concurrent HTTP requests.

An automated end-to-end version of this exact lock-wait demonstration is:

```bash
./scripts/test_concurrency_lab.sh
```

Keep the backend running while invoking it. Set `MINIDB_API_URL` if the backend uses a non-default port.

## Demonstrate CPU scheduling algorithms and the Linux scheduler

### Textbook CPU scheduling simulator

1. Open **OS Scheduling Lab** in the dashboard.
2. In **CPU Scheduling Algorithm Simulator**, edit the process table. Fields are PID, Arrival Time, Burst Time, Priority and Queue (for MLQ).
3. Select one of nine algorithms: FCFS, SJF, SRTF, Priority non-preemptive, Priority preemptive, Round Robin, HRRN, MLQ or MLFQ.
4. For Round Robin, choose a time quantum. Smaller numeric priority means higher priority.
5. Click **Run selected algorithm** for the Gantt chart and CT/TAT/WT/RT result table. Click **Compare all algorithms** to compare average waiting, turnaround and response time on the same workload.
6. MLQ uses three fixed queues (Q0 RR-2, Q1 RR-4, Q2 FCFS). MLFQ uses 3 levels with quanta 2/4/8 and a global priority boost every 30 time units. These policies are stated in the UI and `docs/cpu_scheduling_lab.md`.

The textbook simulator is a deterministic educational model; it does not control the host OS scheduler and does not charge CPU time for context-switch overhead.

### Actual Linux worker scheduling measurement

1. In the same **OS Scheduling Lab**, scroll to **CPU-bound scheduling experiment**.
2. Choose 1, 2 or 3 worker threads and a workload duration.
3. Click **Start CPU scheduling experiment**.
4. The server submits CPU-bound tasks to its real long-lived WorkerPool threads. Each task records its Linux TID and before/after scheduler counters.
5. Review scheduling policy, priority/nice value, last CPU, CPU-time delta, run-queue-wait delta, timeslice delta and context-switch deltas.

The normal Linux `SCHED_OTHER` policy is expected on an ordinary desktop. This experiment observes the host Linux scheduler; it does not pretend that MiniDB has replaced Linux's scheduler with FCFS/SJF/Round Robin. Values depend on the kernel, CPU availability and system load. `schedstat` may be unavailable or disabled on some kernels. `docs/cpu_scheduling_lab.md` explains the algorithm assumptions and formulas.

Automated API verification:

```bash
./scripts/test_scheduler_api.sh
```

Keep the backend running. This test checks that three distinct WorkerPool TIDs execute the CPU-bound jobs and that scheduler result records are returned.

## API endpoints

| Method | Endpoint | Purpose |
|---|---|---|
| GET | `/api/health` | Health check |
| GET | `/api/metrics` | Runtime metrics: workers, TIDs, queue, buffer pool, sessions, transactions, lock table, wait-for edges and WAL counters |
| POST | `/api/sessions` | Create an independent SQL session |
| GET | `/api/sessions` | List active session IDs |
| DELETE | `/api/sessions/{id}` | Close a session |
| POST | `/api/sql` | Execute SQL for one session |
| POST | `/api/experiments/scheduler/start` | Start a CPU-bound scheduler measurement |
| GET | `/api/experiments/scheduler/status` | Read experiment status/results |

Example:

```bash
curl -s http://127.0.0.1:8080/api/health
curl -s http://127.0.0.1:8080/api/metrics | python3 -m json.tool
curl -X POST http://127.0.0.1:8080/api/sessions \
  -H 'Content-Type: application/json' -d '{}'
```

## Verification

A one-command integrated check is available:

```bash
./scripts/verify.sh
```

It builds the project, runs CTest and telemetry/procfs checks, starts a temporary backend instance, and runs the concurrent-lock and scheduler API integration tests. It uses a separate verification port and the project's `data/` directory; the concurrency test creates a uniquely named demo table.

For individual checks:

```bash
./scripts/build.sh
ctest --test-dir build --output-on-failure
./scripts/test_telemetry.sh
./scripts/test_cpu_scheduling.sh
```

The live API integration scripts require a running backend:

```bash
./scripts/test_concurrency_lab.sh
./scripts/test_scheduler_api.sh
```

## Supported SQL subset

The lexer/parser/executor support the current basic table and row operations: `CREATE TABLE`, `DROP TABLE`, `INSERT`, `SELECT`, `UPDATE`, `DELETE`, `BEGIN`, `COMMIT`, `ROLLBACK`, predicates, projections, ordering, limit/offset and the implemented aggregate subset. Check the parser/executor source and tests for the exact grammar. SQL `JOIN`, `GROUP BY` and `HAVING` are not currently implemented by the executor.

## Important engineering limitations

- The catalog/table-definition metadata is still process-memory state in this source version. Do not assume that table names and schemas automatically survive a server restart just because the page file survives.
- The `RecoveryManager` has simplified analysis/REDO/UNDO behaviour and standalone crash-recovery tests. It is not a complete ARIES implementation, and the current server entry point does not automatically run full recovery with catalog/table callbacks at startup.
- Full ARIES components such as pageLSN-based redo skipping, Compensation Log Records, full checkpoint dirty-page/transaction-table analysis and torn-page protection are not implemented.
- SQL JOIN, GROUP BY, HAVING, foreign keys and secondary indexes are not implemented by the current executor.
- Linux scheduling telemetry is an observation/experiment, not a custom OS scheduler. The scheduler experiment uses CPU-bound tasks, actual worker TIDs and Linux counters; it does not produce a fabricated per-core Gantt chart.
- The project is single-server-process educational software. Multi-process coordination, hostile-network hardening, comprehensive SQL type semantics and production-grade crash-safe DDL are out of scope.

## Study material

Start with `docs/MINIDB_FROM_SCRATCH_HINGLISH.md`, then read:

- `docs/architecture.md`
- `docs/execution_flow.md`
- `docs/concurrency_and_os_lab.md`
- `docs/os_concepts.md`
- `docs/cpu_scheduling_lab.md`
- `docs/dbms_concepts.md`
- `docs/viva_questions.md`

## Understanding the dashboard

For beginner-friendly explanations of WorkerPool, request queue, CPU simulation time vs real experiment duration, Session vs Transaction vs Linux TID, disk page inventory, and the live wait-for graph demo, read:

- `docs/DASHBOARD_EXPLAINED_HINGLISH.md`
