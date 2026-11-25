# MiniDB — Spoon-fed Hinglish Study Guide

Ye guide project ko zero se samjhane ke liye hai. Isko order mein padho. Pehle overall flow samjho, phir ek-ek module.

## 1. MiniDB kya hai?

MiniDB ek educational database management system (DBMS) hai jo hum C++17 mein bana rahe hain. Iska kaam SQL statements lena, unhe samajhna, records ko disk par store/read/update/delete karna, multiple clients ko handle karna aur failures ke baad data ko recover karne ki foundation provide karna hai.

SQLite use karke query forward karna iska goal nahi hai. Storage, buffer pool, SQL parsing, transaction management aur concurrency mechanisms apne code mein implemented hain.

Simple analogy: DBMS ek library jaisa hai. SQL query book ka naam hai, Catalog library ka register hai, TableHeap shelf hai, DiskManager building ka permanent storage hai, aur BufferPool woh books hain jo librarian ne desk par temporarily rakhi hain.

## 2. Overall request flow

```text
Browser / curl client
       |
       v
HTTP Server (port 8080)
       |
       v
Bounded Request Queue (capacity 8)
       |
       v
3 Worker Threads
       |
       v
HTTP API -> Session Manager -> SQL Engine
                              |
                       Lexer -> Parser
                              |
                           Executor
                              |
               Transaction + Lock Manager
                              |
                  TableHeap / BufferPool
                              |
                         DiskManager
                              |
                         Database file

Changes also generate WAL records through LogManager.
```

Yahi project ka central architecture hai. Browser HTTP request bhejta hai; SQL Engine query execute karta hai; lower storage layers disk par data manage karte hain.

## 3. Process, thread, session aur transaction mein difference

**Process:** Linux mein chal raha MiniDB server program. Iska apna address space aur PID hota hai.

**Thread:** Process ke andar execution path. MiniDB worker pool mein 3 C++ worker threads hain. Linux har thread ko TID deta hai. Ek process ke threads resources share karte hain.

**Session:** Ek SQL client ki logical connection identity. Har session ka alag ID hota hai. Session ke andar explicit transaction track hoti hai. Session aur thread same cheez nahi hain: ek session ki do requests alag-alag worker threads par run ho sakti hain.

**Transaction:** SQL operations ka logical group. BEGIN ke baad changes COMMIT tak transaction ke part hain; ROLLBACK ya disconnect par uncommitted changes undo kiye jaate hain (current implementation ke supported operations ke andar).

Yaad rakho: process > threads; session client identity hai; transaction logical unit of work hai.

## 4. Producer-consumer queue aur semaphore

HTTP server request ko producer ke role mein queue mein daalta hai. Worker threads consumers hain jo queue se request nikaal kar execute karte hain.

Queue ki capacity 8 hai. Iska matlab maximum 8 tasks pending ho sakte hain. Ye 8 total clients ya 8 worker threads ka matlab nahi hai.

Do POSIX semaphores use hote hain:

- `available_slots` initially 8: kitni jagah khaali hai.
- `available_items` initially 0: kitne requests ready hain.

Mutex queue data structure ko protect karta hai, taaki multiple threads same time par `std::queue` ko corrupt na karein.

**Semaphore vs mutex:** semaphore resource count track karta hai; mutex critical section mein ek waqt par ek thread ko enter hone deta hai.

## 5. Worker Pool

WorkerPool fixed number of worker threads create karta hai. MiniDB configuration mein 3 workers hain. HTTP accept loop incoming socket ko `Submit()` karta hai. Worker queue se task uthata hai, HTTP request parse karta hai, API call karta hai aur response bhejta hai.

`future` ka purpose task result ko baad mein receive karna hai. `packaged_task` task ke return value ya exception ko future mein store karta hai.

Shutdown mein pehle naye tasks reject hote hain, phir sentinel tasks enqueue hote hain aur threads join hoti hain. Isse queued tasks drain ho sakte hain.

## 6. HTTP API

Current routes:

| Route | Purpose |
|---|---|
| `GET /api/health` | Server health response |
| `GET /api/metrics` | Runtime/kernel metrics snapshot |
| `POST /api/sessions` | New SQL session |
| `GET /api/sessions` | Active session IDs |
| `DELETE /api/sessions/{id}` | Session close |
| `POST /api/sql` | SQL execute |

`POST /api/sql` body example:

```json
{
  "session_id": 1,
  "sql": "SELECT * FROM students;"
}
```

API SQL text ko parse nahi karti; woh `SqlEngine` ko delegate karti hai. API ka kaam HTTP/JSON ko engine calls aur engine result ko JSON mein convert karna hai.

## 7. SQL Lexer, Parser aur Executor

**Lexer:** SQL text ko tokens mein todta hai. Example `SELECT id FROM students;` mein SELECT keyword, id identifier, FROM keyword, students identifier, semicolon token bante hain.

**Parser:** tokens ko grammar ke according AST (Abstract Syntax Tree) mein convert karta hai. AST query ka structured representation hota hai. Parser precedence handle karta hai: multiplication addition se pehle, AND OR se pehle etc.

**Executor:** AST ko actual operations mein convert karta hai. SELECT par records scan hote hain, WHERE evaluate hota hai, projection/sort/limit apply hota hai. INSERT/UPDATE/DELETE TableHeap aur transactions ke through records modify karte hain.

Lexer = words pehchano; Parser = sentence structure samjho; Executor = actual kaam karo.

## 8. Catalog, Schema aur Tuple

**Catalog:** Database ke table definitions ka register. Table name, ID aur schema metadata store karta hai. Current source version mein Catalog process memory mein hai; data pages file mein likhe ja sakte hain, lekin table name/schema/root-page mapping automatically restart ke baad restore nahi hoti. Isliye persistence ke claims carefully karne hain.

**Schema:** Table ke columns ka blueprint, jaise `id INT`, `name VARCHAR(50)`.

**Tuple:** Ek row. Example `(1, 'Aman')`. Tuple values validate/serialize/deserialize karta hai.

## 9. DiskManager aur 4 KB pages

DiskManager database file ko page-size units mein read/write karta hai. Project page size 4096 bytes hai.

Page ID ko disk file ke offset mein convert kiya jaata hai:

`offset = page_id * 4096`

Isliye page 0 byte 0 se, page 1 byte 4096 se, page 2 byte 8192 se start hota hai.

`pread/pwrite` specific offset par read/write karte hain. `fsync` operating system ko data durable storage tak flush karne ko kehta hai.

## 10. SlottedPage aur TableHeap

**SlottedPage:** ek 4096-byte page ke andar variable-size records aur slot directory manage karta hai. Slot ID record ko locate karne mein help karta hai. Record bytes ko move/compact karne par bhi slot identity stable rakhne ka design hai.

**TableHeap:** table ke records ko multiple pages mein manage karta hai. RID (Record ID) page ID + slot ID se record identify karta hai. TableHeap page directory ke through data pages locate karta hai.

Record page ke andar fit na ho to another page use ho sakta hai. Current transactional insert path correctness-first hai aur fresh data page per inserted row allocate kar sakta hai; ye storage-efficient final design nahi hai.

## 11. BufferPool — memory cache

Disk slow hoti hai. Isliye frequently used pages RAM mein cache karte hain. Isi cache ko BufferPool kehte hain.

MiniDB configuration:
- 4 frames.
- Har frame 4096 bytes.
- Total configured cache capacity = 4 × 4096 = 16384 bytes = 16 KiB.

Frame ek RAM slot hai jo ek database page ko hold karta hai. Page table page ID se frame ID map karti hai. Pin count batata hai kitne users currently page hold kar rahe hain. Pinned page evict nahi karna chahiye.

Dirty page ka matlab RAM copy disk copy se newer hai. Flush karte waqt WAL-before-data rule follow karna chahiye: relevant WAL durable hone ke baad hi dirty data page disk par write ho.

LRU (Least Recently Used) policy least-recently-used unpinned page ko eviction candidate choose karti hai.

## 12. Transactions aur ACID

Transaction ek logical unit hai. ACID:

- Atomicity: all-or-nothing behaviour.
- Consistency: constraints/rules preserve rehne chahiye.
- Isolation: concurrent transactions ek doosre ke intermediate work ko improperly observe na karein.
- Durability: commit ke baad change crash ke baad bhi survive karna chahiye.

MiniDB mein `BEGIN`, `COMMIT`, `ROLLBACK` support hai. Autocommit statements apne aap transaction complete karte hain. Explicit transaction multiple statements ko group karti hai.

## 13. Locks, Strict 2PL aur deadlock

Shared (S) lock reading ke liye hota hai. Multiple readers same resource ko shared-lock kar sakte hain.

Exclusive (X) lock writing ke liye hota hai. X lock ke saath same resource par incompatible lock allow nahi hota.

Strict Two-Phase Locking (Strict 2PL) mein transaction ke acquired locks commit/abort tak hold kiye jaate hain. Isse uncommitted writes read/write karne ke risk ko reduce karte hain aur recoverability improve hoti hai.

**Deadlock:** T1 T2 ka wait kar raha hai aur T2 T1 ka wait kar raha hai; dono progress nahi kar paate.

Wait-for graph mein edge A → B ka matlab A, B ka wait kar raha hai. Graph mein cycle deadlock indicate karti hai. Detector victim transaction select karta hai; rollback ke baad locks release hote hain.

## 14. WAL aur Crash Recovery

WAL = Write-Ahead Log. Data page ko disk par likhne se pehle us change ka log record durable hona chahiye.

Common log records:
- BEGIN: transaction start.
- INSERT/UPDATE/DELETE: operation record, page/slot aur images ke saath.
- COMMIT: commit record.
- ABORT: rollback complete hone ka record.

LSN (Log Sequence Number) current log mein record ka byte offset identify karta hai.

Recovery concepts:
- REDO: committed changes ko dobara apply karo agar data page par missing hon.
- UNDO: incomplete/uncommitted transaction ke changes reverse karo.

Current tests ne do scenarios verify kiye: commit log durable ho aur data page flush na hua ho to REDO; uncommitted page disk par flush ho aur process crash ho to UNDO.

Important limitation: current recovery simplified hai, full ARIES nahi. PageLSN, Compensation Log Records, full checkpoint analysis aur torn-page protection missing hain. RecoveryManager ke standalone crash tests aur algorithms ko server startup par real Catalog/TableHeap callbacks ke saath automatically wire karna abhi remaining integration task hai.

## 15. Dashboard metrics ka meaning

- Buffer Pool: frame capacity, resident pages aur pinned frames.
- Concurrency: active transactions, current wait-for graph edges, LockManager ka physical RID lock table, holders aur waiters.
- OS Monitor: worker Linux TID, worker busy/idle state, last CPU core, cumulative CPU time aur voluntary/nonvoluntary context switches.
- Scheduler Experiment: CPU-bound task chalne se pehle/baad per-worker CPU time, Linux scheduling policy/priority, `/proc/.../schedstat` runtime, run-queue wait, timeslice aur context-switch deltas.
- Storage: database file page count/size aur current process catalog ke table names.
- WAL: file size, next LSN aur persistent LSN.

CPU time cumulative hai; ye instantaneous CPU percentage nahi. Last CPU core `/proc` ka last recorded processor hai, hard pinning/affinity nahi.

## 16. Current supported scope vs remaining engineering work

Current implemented/tested areas include storage pages, buffer pool, WAL manager, transaction/lock/deadlock foundations, lexer/parser, basic SQL CRUD, independent sessions, bounded queue, three worker threads, HTTP SQL API, terminal client, two-client HTTP lock-wait test, and Linux scheduler/procfs experiment on actual worker TIDs.

The following must not be presented as complete yet:

1. Persistent Catalog and table-root metadata across server restart.
2. RecoveryManager automatic invocation at server startup with real table/page callbacks.
3. Full ARIES recovery (pageLSN, CLRs, complete checkpoint analysis and torn-page protection).
4. SQL JOIN, GROUP BY and HAVING execution; tokens alone do not mean execution support.
5. Dirty-page counters, buffer hit/miss ratio, eviction counters and historical deadlock telemetry.
6. Production-grade multi-process coordination, network hardening and crash-safe DDL.
7. A production-grade custom kernel scheduler. MiniDB provides a textbook CPU scheduling simulator (FCFS, SJF, SRTF, Priority, RR, HRRN, MLQ, MLFQ) plus a separate real-Linux worker scheduling measurement. The browser simulator does not replace the kernel scheduler.

## 17. Concurrent SQL Lab — two real client sessions

Ye important demo hai kyunki yahin hum prove karte hain ki do clients sirf alag text boxes nahi hain; unke server-side session IDs aur transaction contexts bhi separate hain.

### Demo karne ka exact order

1. Backend run karo: `./scripts/run_server.sh`.
2. Dashboard run karo: `./scripts/run_dashboard.sh`, phir `http://127.0.0.1:5500` kholo.
3. Sidebar se **Concurrency Lab** open karo.
4. **Create / reconnect Client A & B** press karo. Dono ke session IDs different hone chahiye.
5. **Setup accounts demo table** ek baar press karo. Ye `accounts(id, balance)` table aur `(1,1000)` row banata hai.
6. Sirf Client A ka Run button press karo:

   ```sql
   BEGIN;
   UPDATE accounts SET balance = balance - 100 WHERE id = 1;
   ```

   Request complete ho jayegi lekin transaction open rahegi. A ka write-lock commit tak hold hota hai.

7. Ab Client B ka Run button press karo:

   ```sql
   BEGIN;
   UPDATE accounts SET balance = balance - 50 WHERE id = 1;
   ```

   B ko same record par compatible lock nahi milta, isliye uska HTTP request pending/waiting state mein rehta hai.

8. Lock table mein physical RID, lock holder aur waiter dekho. Wait-for graph mein edge `T_B -> T_A` ka matlab B, A ke lock release ka wait kar raha hai.
9. Client A ke Commit button ko press karo. A ke locks release hote hain aur B continue kar sakta hai.
10. B complete hone par Client B Commit press karo. Phir kisi client se `SELECT * FROM accounts;` run karo. Expected balance `850` hai.

**Run both clients together** button alag concurrency/race experiment hai. Lock wait demonstrate karne ke liye above sequential submission order use karo: pehle A hold kare, phir B wait kare, phir A commit kare.

### Backend ke andar kya hota hai?

```text
Client A / Session S1 ─┐
                       ├─ HTTP Server -> bounded queue -> 3 Worker Threads
Client B / Session S2 ─┘                          │
                                            SQL Engine
                                           /         \
                                   Transaction A   Transaction B
                                           \         /
                                          LockManager
                                             │
                                       Wait-for Graph
```

Ek session ek dedicated thread nahi hota. A aur B ke requests worker pool ke different threads par execute ho sakte hain. Jab B lock ke liye wait karta hai, ek worker blocked hota hai; doosra worker A ka COMMIT request process kar sakta hai, aur teesra worker available ho sakta hai.

Automated live integration test (backend running hona chahiye):

```bash
./scripts/test_concurrency_lab.sh
```

## 18. CPU Scheduling Lab — textbook algorithms + real Linux

OS Scheduling Lab ke top par **CPU Scheduling Algorithm Simulator** hai. Ismein ye 9 algorithms run kar sakte ho:

1. **FCFS:** First Come First Served, non-preemptive.
2. **SJF:** Shortest Job First, non-preemptive.
3. **SRTF:** Shortest Remaining Time First, preemptive SJF.
4. **Priority Non-preemptive:** highest-priority ready process ko completion tak CPU milta hai.
5. **Priority Preemptive:** higher-priority process aane par current process preempt ho sakta hai.
6. **Round Robin:** fixed time quantum; unfinished process ready queue ke end par jaata hai.
7. **HRRN:** Highest Response Ratio Next; `(Waiting + Burst) / Burst` maximum choose hota hai.
8. **MLQ:** fixed queue assignment; Q0 = RR-2, Q1 = RR-4, Q2 = FCFS. Lower queue number higher precedence.
9. **MLFQ:** processes Q0 se start; 2/4/8 time quantum par full slice consume karne ke baad demotion; every 30 units priority boost.

### Simulator kaise use karna hai

1. Dashboard sidebar se **OS Scheduling Lab** open karo.
2. Process table mein PID, Arrival Time (AT), Burst Time (BT), Priority aur Queue (MLQ) set karo.
3. Algorithm select karo. RR ke liye quantum set karo. Priority mein smaller number higher priority hai.
4. **Run selected algorithm** press karo. Gantt chart aur per-process result table milenge.
5. **Compare all algorithms** press karke same input par 9 algorithms ke average WT, TAT, RT, CPU utilization, throughput aur process-switch count compare karo.

Formulas:

- CT = Completion Time.
- TAT = CT − AT.
- WT = TAT − BT.
- RT = First CPU Start − AT.
- CPU Utilization = Busy Time / elapsed time from t=0 to final completion × 100.
- Throughput = completed processes / elapsed time.

**Important:** Simulator ke Gantt chart mein process switches educational schedule transitions hain; ye Linux kernel ke context-switch counter nahi hain. Context-switch overhead bhi simulated CPU time se deduct nahi hota. MLQ/MLFQ ek specified teaching configuration hai; OS texts/implementations alag policies use kar sakte hain.

### Real Linux worker scheduling experiment

Simulator ke neeche actual Linux experiment hai:

1. Worker count 1, 2 ya 3 select karo aur duration choose karo.
2. **Start CPU scheduling experiment** press karo.
3. Server CPU-bound jobs ko existing MiniDB WorkerPool threads par submit karta hai. Barrier ki wajah se tasks near-simultaneously start karte hain.
4. Har worker ke actual Linux TID par before/after counters read hote hain.

Metrics ka meaning:

- **Linux TID:** kernel thread identifier.
- **Policy:** `SCHED_OTHER` usually normal Linux fair-scheduling class hai; policy actual `pthread_getschedparam` se read hoti hai.
- **CPU time delta:** experiment duration mein worker thread ne kitna CPU time consume kiya.
- **Last CPU:** kernel ne us thread ke liye last kaunsa CPU number record kiya.
- **Run-queue wait delta:** thread runnable/ready tha, lekin CPU ke liye wait kiya; `/proc/self/task/TID/schedstat` se milta hai.
- **Timeslices delta:** scheduler ke recorded timeslice/scheduling count ka delta.
- **Context switches:** voluntary aur involuntary context-switch counter differences.

Ye real host Linux scheduler ko observe karta hai; textbook simulator se alag hai. Normal desktop par `SCHED_OTHER` expected hai. Kernel config, number of CPUs aur background load results ko affect karte hain. Schedstat kuch kernels par disabled/unavailable ho sakta hai. Linux worker experiment user-space se FCFS/SJF/RT priorities set nahi karta.

Automated live API test (backend running hona chahiye):

```bash
./scripts/test_scheduler_api.sh
```

## 19. Demo commands and terminal clients

Integrated verification (build + CTest + procfs/telemetry + two live API demos):

```bash
./scripts/verify.sh
```

Start backend:

```bash
./scripts/run_server.sh
```

Open dashboard in another terminal:

```bash
./scripts/run_dashboard.sh
```

Browser: `http://127.0.0.1:5500`

Open an independent terminal SQL session:

```bash
./scripts/run_client.sh
```

Do this in two terminals to create two independent terminal sessions. In the client, SQL statements end with `;`; `:commit`, `:rollback`, `:help` and `:quit` are client commands.

Check health and runtime metrics:

```bash
curl -i http://127.0.0.1:8080/api/health
curl -s http://127.0.0.1:8080/api/metrics | python3 -m json.tool
```

## 20. Viva quick questions

**Q: Why do we need a BufferPool?**
A: To cache disk pages in RAM and avoid repeatedly reading the same pages from disk.

**Q: What is a frame?**
A: One fixed-size slot in BufferPool that stores one database page.

**Q: Why is the queue bounded?**
A: To cap pending work and apply back-pressure instead of allowing unbounded memory growth.

**Q: What is the difference between semaphore and mutex?**
A: Semaphore tracks available resource count; mutex protects a critical section.

**Q: What does a Linux TID show?**
A: It identifies an individual kernel thread within a process.

**Q: Why use WAL?**
A: To make updates recoverable; the log is made durable before dirty data pages are written.

**Q: REDO vs UNDO?**
A: REDO reapplies needed committed effects; UNDO reverses effects of incomplete transactions.

**Q: What is a deadlock?**
A: A cycle of transactions waiting for each other's locks.

**Q: What is an RID?**
A: A Record Identifier containing a page ID and slot ID.

**Q: Does one session equal one worker thread?**
A: No. A session is a logical client context; requests from it may execute on different worker threads.


## 20. CPU Scheduling Lab — quick revision

Dashboard mein OS Scheduling Lab open karo. Top par process input table aur algorithm simulator hai. Current simulator mein FCFS, SJF, SRTF, Priority non-preemptive, Priority preemptive, Round Robin, HRRN, MLQ aur MLFQ included hain. `docs/cpu_scheduling_lab.md` mein har algorithm ka rule, MLQ/MLFQ variant aur formula detail mein diya hai.

Same input sab algorithms ko dene ke liye **Compare all algorithms** use karo. Isse average WT, TAT, RT, CPU utilization, throughput aur process-switch count compare hote hain. Green value us comparison mein lowest metric ko indicate karti hai.

Algorithm simulator ke Gantt chart ko actual Linux scheduler trace mat bolna. Page ke lower section mein separate Linux worker experiment hai jo real TIDs aur kernel counters measure karta hai.
