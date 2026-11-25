# MiniDB Concurrent SQL Lab and Linux Scheduler Lab

## Concurrent SQL Lab

The Concurrency Lab creates two independent MiniDB SQL sessions. Each browser client keeps its own session ID and transaction context. The two `/api/sql` requests are sent independently to the HTTP server, where different WorkerPool threads may execute them concurrently.

### Lock-wait demonstration

1. Start the MiniDB server and open the dashboard.
2. Open **Concurrency Lab**.
3. Click **Setup accounts demo table** once. It creates `accounts(id, balance)` with one row `(1, 1000)`.
4. Run Client A:

   ```sql
   BEGIN;
   UPDATE accounts SET balance = balance - 100 WHERE id = 1;
   ```

   This request completes but leaves A's transaction open. Strict 2PL retains its record lock until COMMIT/ROLLBACK.

5. Run Client B:

   ```sql
   BEGIN;
   UPDATE accounts SET balance = balance - 50 WHERE id = 1;
   ```

   Its HTTP request remains in progress while its transaction waits for a compatible lock on the same record.
6. Observe **Lock Table & Wait-for Graph**. The wait-for graph should show `waiting transaction -> blocking transaction`; the lock table shows physical RID (page, slot), current holder and waiting mode.
7. Click **Commit** on Client A. Its request is handled by another WorkerPool thread, releases A's lock, and lets B proceed.
8. Commit Client B and run `SELECT * FROM accounts;`. The expected balance is `850`.

The lock manager keys records using physical RID, so this dashboard shows `RID(page, slot)`, not a logical SQL key such as `accounts.id=1`. The wait-for edge and waiter state are live snapshots, not hard-coded demo data.

You can run the equivalent automated integration test while the server is active:

```bash
./scripts/test_concurrency_lab.sh
```

## Linux CPU scheduling experiment

Open **OS Scheduling Lab** and select 1, 2 or 3 workers and a duration. The start endpoint queues CPU-bound tasks onto the existing MiniDB WorkerPool. A barrier releases the tasks together so they run on distinct long-lived worker threads. The API records each Linux TID and compares before/after values from:

- `/proc/self/task/<tid>/stat` and `status`: CPU time, last reported CPU, voluntary and involuntary context switches.
- `/proc/self/task/<tid>/schedstat`: CPU runtime, run-queue wait time and timeslice count, when enabled by the host kernel.
- `pthread_getschedparam`: actual scheduling policy and real-time priority; nice value is also sampled for the current Linux thread.

The result is a measured delta, not a fabricated CPU Gantt chart. `SCHED_OTHER` is normal for Linux CFS/fair-scheduler threads. The host kernel, available CPUs and other system load affect values.

Run the endpoint integration check while the server is active:

```bash
./scripts/test_scheduler_api.sh
```

## Terminal clients

Start two terminals and run `./scripts/run_client.sh` in both. Each process requests its own server-side session. They can issue independent transactions and demonstrate locking without sharing browser state.


## Interpreting the dashboard

- **Worker Pool (3)** means three reusable C++ worker threads. It does not mean three CPU cores. Up to three HTTP request tasks execute at once; up to eight additional tasks can wait in the bounded queue.
- **Session** is a client connection with its own transaction context. Session ID (S1) is not a Linux TID and not a Transaction ID (T1). A session can be idle or can own one active transaction.
- **CPU simulator time** (AT, BT and RR quantum) uses abstract simulation units. The real Linux experiment's duration selector is wall-clock requested workload duration in seconds; it is not a Round Robin quantum.
- **Wait-for graph** only has edges during a real lock wait. An edge T2 → T1 means T2 is blocked by a lock held by T1. Run the guided demo: Client A BEGIN + UPDATE; Client B same-row BEGIN + UPDATE; inspect the graph while B waits; COMMIT A to unblock B.
- **Storage explorer** lists the actual database file size and the first 256 allocated 4096-byte pages. `THDR` marks a TableHeap directory page and `SLPG` marks a slotted data page. Table rows can be inspected with SELECT in the SQL Workbench. Catalog definitions in this release are still process-memory-only; the page browser does not claim persistent catalog support.
