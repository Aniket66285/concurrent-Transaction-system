# MiniDB Dashboard — Hinglish Guide (Beginner Friendly)

Ye guide dashboard par dikhne wale numbers aur panels ka meaning explain karti hai. Iska purpose sirf buttons chalana nahi, balki engine ke andar kya ho raha hai samajhna hai.

## 1. Worker Pool = 3 ka matlab

MiniDB server mein ek `WorkerPool` object hai. Iske andar 3 long-lived C++ worker threads create hote hain: W1, W2 aur W3.

- HTTP server client connection accept karta hai.
- Har accepted HTTP request ko bounded request queue mein task ke roop mein dala jaata hai.
- Koi free worker task pop karke request handle karta hai.
- Task complete hone par worker next task le sakta hai.

**3 workers ka matlab 3 CPU cores nahi hai.** Teen threads same CPU core par time-share kar sakte hain, ya OS unhe multiple available cores par schedule kar sakta hai. Linux `TID` har worker ka kernel thread ID hai.

Agar 3 worker busy hon, next request queue mein wait karegi. Queue ki maximum capacity 8 pending tasks hai. Ye 8 active workers nahi hain.

| Dashboard item | Meaning |
|---|---|
| W1 / W2 / W3 | MiniDB ke reusable C++ worker threads |
| TID | Linux kernel thread ID |
| BUSY | Worker abhi ek request/task execute kar raha hai; SQL lock par wait karte waqt bhi task busy count hota hai |
| IDLE | Worker queue se next task ka wait kar raha hai |
| Active tasks | Is waqt workers ke andar chal rahe tasks |
| Pending queue | Queue mein abhi tak pop nahi hue requests |
| Completed tasks | Server process start hone ke baad complete hue tasks |

## 2. Seconds aur time units ka difference

OS Scheduling Lab mein do alag clocks hain:

### A. Textbook CPU scheduling simulator

Arrival Time (AT), Burst Time (BT), aur Round Robin quantum **abstract simulation time units** hain. Ye real wall-clock seconds nahi hain.

Example: P1 ka BT=5 ka matlab simulator ke model mein P1 ko 5 CPU time units chahiye. Agar RR quantum=2 hai, P1 ko pehle 2 units, phir ready queue ke order ke hisaab se CPU milega.

### B. Real Linux CPU workload experiment

`CPU workload duration = 2 seconds` ka matlab experiment selected worker threads ko roughly 2 seconds ke wall-clock interval tak CPU-bound calculation deta hai.

Ye Round Robin quantum nahi hai, aur iska matlab ye bhi nahi ki har thread ko exactly 2 seconds CPU time milega. Actual CPU time, run-queue wait, timeslices aur context switches Linux counters ke before/after difference se report hote hain. Host par kitne CPU cores hain aur doosre processes kitna load daal rahe hain, result par effect padta hai.

## 3. Session, Transaction aur Thread alag cheezein hain

In teen IDs ko mix mat karna:

- **Session ID (S1):** SQL client connection ki identity. Session ka apna transaction context hota hai.
- **Transaction ID (T1):** `BEGIN` se start hone wali logical unit of work ki identity. `COMMIT` ya `ROLLBACK` par transaction finish hoti hai.
- **Linux TID:** OS scheduler ke liye C++ worker thread ki identity.

Ek SQL client session ki request alag-alag time par W1, W2 ya W3 par execute ho sakti hai. Session kisi ek worker thread ka permanent owner nahi hota.

Sessions page par:

- `IDLE`: session currently kisi explicit transaction ko hold nahi kar raha.
- `ACTIVE` with Tn: session ke paas active transaction Tn hai.
- `BUSY / EXECUTING`: is waqt us session ki SQL request execute ho rahi hai. Lock wait mein blocked request bhi busy hoti hai.
- `Close session`: session close karta hai; active transaction ho toh SQL Engine rollback karta hai.

## 4. Storage page par kya dekhna hai

Pehle SQL Workbench se table aur row create karo:

```sql
CREATE TABLE students (id INT, name VARCHAR(50));
INSERT INTO students VALUES (1, 'Aman');
```

Phir Storage page open karo. Is release mein page explorer database file ke first 256 allocated pages inspect karta hai.

- `DATABASE FILE`: database file ka actual size/path.
- `ALLOCATED PAGES`: DiskManager ke file mein allocated 4096-byte pages.
- `THDR`: TableHeap directory page ka magic header.
- `SLPG`: Slotted data page ka magic header.
- `Unknown / system page`: koi doosra page format ya header.
- `TABLE SCHEMA`: table ke column names, types aur nullable status.

**Actual row values dekhne ke liye SQL Workbench mein `SELECT * FROM students;` chalao.** Page inventory raw page type batata hai; ye record browser nahi hai.

Current release ka Catalog table definitions process memory mein rakhta hai. Isliye database data pages disk par dikh sakte hain, lekin server restart ke baad in-memory table names/schema automatically reconstruct hona abhi implemented nahi hai. Storage page is limitation ko clearly show karta hai.

## 5. Wait-for Graph kaise dikhana hai

Wait-for graph idle state mein empty hona correct hai. Graph tabhi edge dikhata hai jab ek transaction doosre transaction ke lock ka wait kar rahi ho.

Dashboard par **Concurrent SQL Lab → Start guided lock-wait demo** use karo. Ye sequence automate karega:

1. Do independent sessions Client A aur Client B connect honge.
2. `accounts` table/row `id=1` ensure kiya jayega.
3. Client A `BEGIN` karega aur `balance` update karke transaction open rakhega. A exclusive lock hold karega.
4. Client B same row update karega. Uski request lock ke liye wait karegi.
5. Graph mein edge **T2 → T1** (actual IDs session ke hisaab se different ho sakte hain) appear hogi.
6. Left node WAITING transaction hai; right node BLOCKING transaction hai.
7. Client A ka COMMIT button press karo. A lock release karega, B resume karega.
8. Client B ka COMMIT press karo. Final balance initial 1000 se 100 kam aur 50 kam = 850 hoga.

Direction yaad rakho: `T2 → T1` ka matlab T2, T1 ke lock ka wait kar raha hai. Arrow ka source waiter aur destination blocker hai.

Agar edge nahi dikhti:
- Confirm A ka `BEGIN + UPDATE` successful hua aur transaction active hai.
- B ko same table ke same row (`id=1`) par update karna chahiye.
- B ka result `RUNNING/WAITING` hona chahiye.
- Graph page live metrics se refresh hota hai; `Refresh live state` use kar sakte ho.
- Demo ke beech A ka COMMIT jaldi mat dabao; graph edge remove ho jayegi.

## 6. Buffer Pool aur Storage mein difference

- **Disk storage:** database file mein persistent 4 KB pages.
- **Buffer Pool:** disk pages ki RAM cache. Is project mein 4 frames × 4096 bytes = 16384 bytes = 16 KiB configured cache.
- **Resident:** page currently cache mein loaded hai.
- **Pinned:** page koi operation use kar raha hai; pinned page evict nahi karni chahiye.

Buffer Pool chhota RAM cache hai; poora database nahi. Isliye allocated disk pages buffer frames se zyada ho sakte hain.

## 7. Demo ke liye recommended flow

1. SQL Workbench: CREATE TABLE + INSERT.
2. Storage: table schema aur THDR/SLPG pages inspect karo.
3. Sessions: naya session create karo; uska ID aur transaction state dekho.
4. Concurrent SQL Lab: guided lock-wait demo chalao; wait-for edge observe karo; A COMMIT; B COMMIT.
5. OS Scheduling Lab: FCFS/SJF/SRTF/RR simulator mein Gantt chart banao.
6. Real Linux experiment: 3 workers aur 2 seconds select karo; CPU-time/run-queue/timeslice/context-switch deltas compare karo.

## 8. Honesty / scope notes

- CPU algorithm Gantt chart educational simulation hai; kernel ka actual scheduler trace nahi.
- `/proc`/`schedstat` counters actual host Linux se read hote hain, lekin host kernel/container permissions par depend karte hain.
- Lock table resources physical RID (page ID, slot ID) dikhate hain; SQL table/primary-key label mapping abhi implemented nahi.
- Catalog persistence, full ARIES (PageLSN/CLRs/dirty-page-table analysis), JOIN/GROUP BY/HAVING, foreign keys aur secondary indexes abhi remaining advanced work hain.
