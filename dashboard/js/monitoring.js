(() => {
  "use strict";
  const el = (tag, cls, text) => {
    const n = document.createElement(tag);
    if (cls) n.className = cls;
    if (text !== undefined && text !== null) n.textContent = String(text);
    return n;
  };
  const formatBytes = (n) => {
    n = Number(n);
    if (!Number.isFinite(n)) return "—";
    if (n < 1024) return `${n} B`;
    if (n < 1024 ** 2) return `${(n / 1024).toFixed(1)} KB`;
    if (n < 1024 ** 3) return `${(n / 1024 ** 2).toFixed(2)} MB`;
    return `${(n / 1024 ** 3).toFixed(2)} GB`;
  };
  const card = (label, value, help) => {
    const n = el("article", "metric-card");
    n.append(el("div", "metric-label", label), el("div", "metric-value", value), el("div", "metric-help", help || "Live runtime value"));
    return n;
  };
  const grid = (...nodes) => { const n = el("div", "metrics-grid"); nodes.forEach(x => n.appendChild(x)); return n; };
  const table = (headers, rows) => {
    const wrap = el("div", "metrics-table-wrap"), t = el("table", "metrics-table");
    const head = el("thead"), hr = el("tr");
    headers.forEach(h => hr.appendChild(el("th", "", h)));
    head.appendChild(hr);
    const body = el("tbody");
    if (!rows.length) { const tr = el("tr"), td = el("td", "empty-cell", "No records currently reported."); td.colSpan = headers.length; tr.appendChild(td); body.appendChild(tr); }
    rows.forEach(row => { const tr = el("tr"); row.forEach(v => tr.appendChild(el("td", "", v))); body.appendChild(tr); });
    t.append(head, body); wrap.appendChild(t); return wrap;
  };
  const panel = (title, description, content) => {
    const n = el("article", "panel monitor-panel"), h = el("div", "panel-header"), w = el("div");
    w.append(el("h2", "", title), el("p", "", description)); h.appendChild(w); n.append(h, content); return n;
  };
  const put = (id, nodes) => { const root = document.getElementById(id); if (root) root.replaceChildren(...nodes); };

  function renderMemory(m) {
    const b = m.buffer_pool || {}, cap = Number(b.frame_capacity || 0), resident = Number(b.resident_pages || 0), pinned = Number(b.pinned_frames || 0);
    const percent = cap ? Math.min(100, resident / cap * 100) : 0;
    const usage = el("div", "panel memory-usage");
    usage.append(el("div", "metric-help", "Resident frame occupancy"));
    const track = el("div", "usage-track"), fill = el("div", "usage-fill"); fill.style.width = `${percent}%`; track.appendChild(fill);
    usage.append(track, el("div", "metric-help", `${resident} of ${cap} frames resident · ${percent.toFixed(0)}%`));
    put("memory-metrics", [grid(card("FRAME CAPACITY", cap, "Configured buffer frames"), card("RESIDENT PAGES", resident, "Currently cached pages"), card("PINNED FRAMES", pinned, "Frames currently pinned"), card("CACHE CAPACITY", formatBytes(b.capacity_bytes), "Frame count × page size")), panel("Frame occupancy", "Derived from actual BufferPool counters.", usage), el("div", "notice", "Hit ratio, dirty-page count and eviction count are not exposed by the current BufferPool API; they are not fabricated here.")]);
  }
  function renderWaitGraph(edges) {
    const root = el("div", "wait-graph");
    if (!edges.length) {
      root.append(
        el("div", "wait-graph-empty-title", "No active lock waits right now"),
        el("div", "metric-help", "Graph automatically appears while one transaction is blocked by another. Use the two-client guided sequence on the Concurrent SQL Lab page.")
      );
      return root;
    }
    edges.forEach((edge) => {
      const row = el("div", "wait-graph-edge");
      const waiter = el("div", "wait-graph-node waiting");
      waiter.append(el("strong", "", `T${edge.from_txn}`), el("small", "", "WAITING"));
      const connector = el("div", "wait-graph-connector");
      connector.append(el("span", "wait-graph-arrow", "→"), el("small", "", "waits for lock held by"));
      const holder = el("div", "wait-graph-node holding");
      holder.append(el("strong", "", `T${edge.to_txn}`), el("small", "", "BLOCKING"));
      row.append(waiter, connector, holder);
      root.appendChild(row);
    });
    return root;
  }

  function renderConcurrency(m) {
    const c = m.concurrency || {}, sql = m.sql || {}, edges = Array.isArray(c.wait_for_edges) ? c.wait_for_edges : [];
    const locks = Array.isArray(c.lock_table) ? c.lock_table : [];
    const lockRows = [];
    locks.forEach((lock) => {
      const resource = `RID(${lock.page_id}, ${lock.slot_id})`;
      if (Number(lock.exclusive_holder || 0) > 0) lockRows.push([resource, `T${lock.exclusive_holder}`, "X", "GRANTED"]);
      (lock.shared_holders || []).forEach((txn) => lockRows.push([resource, `T${txn}`, "S", "GRANTED"]));
      (lock.waiters || []).forEach((waiter) => lockRows.push([resource, `T${waiter.transaction_id}`, waiter.mode, "WAITING"]));
    });
    put("concurrency-metrics", [
      grid(card("ACTIVE TRANSACTIONS", sql.active_transactions ?? 0, "Transactions registered in TxnManager"), card("WAIT-FOR EDGES", c.wait_for_edge_count ?? edges.length, "Current dependency edges"), card("LOCK REQUESTS WAITING", c.waiting_count ?? 0, "Requests blocked inside LockManager"), card("ACTIVE SESSIONS", sql.sessions ?? 0, "Independent SQL clients")),
      panel("Lock table snapshot", "Physical RID locks copied from the live LockManager. S = shared, X = exclusive.", table(["RESOURCE", "TRANSACTION", "MODE", "STATE"], lockRows)),
      panel("Wait-for graph", "Arrow direction is waiter → blocker. Example: T2 → T1 means T2 cannot continue until T1 releases its lock.", renderWaitGraph(edges)),
      el("div", "notice", "To make an edge appear: Client A runs BEGIN + UPDATE and keeps the transaction open; Client B runs BEGIN + UPDATE on the same row. Leave B waiting, then inspect this graph before committing A. If no transaction is waiting, an empty graph is correct."),
      el("div", "notice", "The demo reports physical RID (page, slot), not SQL table/primary-key names: LockManager's resource identity is the RID.")
    ]);
  }
  function renderOS(m) {
    const w = m.workers || {}, threads = Array.isArray(w.threads) ? w.threads : [];
    const rows = threads.map(t => [t.tid, t.busy ? "BUSY" : "IDLE", t.last_cpu_core < 0 ? "Unavailable" : `CPU ${t.last_cpu_core}`, `${t.cpu_time_ms} ms`, t.voluntary_context_switches, t.nonvoluntary_context_switches, t.readable ? "Readable" : "Unavailable"]);
    put("os-metrics", [grid(card("WORKER THREADS", w.count ?? 0, "Configured C++ worker threads"), card("ACTIVE TASKS", w.active_tasks ?? 0, "Currently executing"), card("QUEUED REQUESTS", `${w.queue_pending ?? 0} / ${w.queue_capacity ?? 0}`, "Pending bounded-queue work"), card("COMPLETED TASKS", w.completed_tasks ?? 0, "Since server process start")), panel("Linux worker threads", "Cumulative kernel counters read from /proc/self/task; BUSY/IDLE reflects actual WorkerPool task state.", table(["LINUX TID", "WORKER STATE", "LAST CPU CORE", "CPU TIME", "VOLUNTARY SWITCHES", "INVOLUNTARY SWITCHES", "PROCFS"], rows)), el("div", "notice", "Last CPU core is the most recent processor recorded by Linux, not a guaranteed current CPU affinity. CPU time is cumulative, not instantaneous utilization. Use the CPU-bound Scheduler Experiment above to compare counter deltas under runnable load.")]);
  }
  function renderStorage(m) {
    const s = m.storage || {};
    const tables = Array.isArray(s.tables) ? s.tables : [];
    const schemas = Array.isArray(s.table_schemas) ? s.table_schemas : [];
    const pages = Array.isArray(s.page_inventory) ? s.page_inventory : [];

    const tableRoot = el("div", "storage-schema-list");
    if (!schemas.length) {
      tableRoot.append(el("div", "empty-state", "No tables in this server process yet. Create a table and insert a row in SQL Workbench to allocate storage pages."));
    } else {
      schemas.forEach((schema) => {
        const cardRoot = el("div", "storage-schema-card");
        cardRoot.append(el("strong", "storage-schema-name", schema.name));
        const columns = Array.isArray(schema.columns) ? schema.columns : [];
        const columnList = el("div", "storage-column-list");
        columns.forEach((column) => columnList.append(el("span", "storage-column-chip", `${column.name} · ${column.type}${column.nullable ? " · NULL" : " · NOT NULL"}`)));
        cardRoot.appendChild(columnList);
        tableRoot.appendChild(cardRoot);
      });
    }

    const pageRows = pages.map(page => [
      `P${page.page_id}`,
      page.kind,
      page.magic,
      formatBytes(page.bytes)
    ]);
    if (s.page_inventory_truncated) pageRows.push(["…", "Inventory capped at first 256 pages", "—", "—"]);

    const pageNote = Number(s.database_pages || 0) === 0
      ? "No physical pages allocated yet. Create a table and INSERT data to see the directory/data pages appear here."
      : "These are actual allocated 4 KB database-file pages. THDR identifies a TableHeap directory page; SLPG identifies a slotted data page.";

    put("storage-metrics", [
      grid(
        card("DATABASE FILE", formatBytes(s.database_bytes), s.database_path || "Configured database file"),
        card("ALLOCATED PAGES", s.database_pages ?? 0, "DiskManager page count · 4096 bytes each"),
        card("CATALOG TABLES", s.table_count ?? tables.length, "Table definitions in this running process"),
        card("PAGE INVENTORY", pages.length, "Physical page headers inspected")
      ),
      panel("Database tables & schemas", "The Catalog tells MiniDB each table's column names and types. Use SELECT in SQL Workbench to inspect the actual row values.", tableRoot),
      panel("Physical page explorer", pageNote, table(["PAGE ID", "PAGE TYPE", "MAGIC HEADER", "SIZE"], pageRows)),
      el("div", "notice warning-note", "Storage truth: table/data pages are written to the database file, but this release's Catalog is process-memory only. A server restart does not reconstruct table definitions yet. The page inventory above shows what is actually allocated on disk; it does not pretend metadata persistence is complete.")
    ]);
  }

  function renderRecovery(m) {
    const w = m.wal || {}, r = m.recovery || {}, state = r.startup_recovery === "not_wired" ? "NOT WIRED" : String(r.startup_recovery || "UNKNOWN");
    put("recovery-metrics", [grid(card("WAL FILE SIZE", formatBytes(w.bytes), w.path || "Write-ahead log"), card("NEXT LSN", w.next_lsn ?? "—", "Next log byte offset"), card("PERSISTENT LSN", w.persistent_lsn ?? "None", "Latest durable WAL record offset"), card("STARTUP RECOVERY", state, "RecoveryManager startup callback status")), panel("Recovery integration", "WAL counters are live; startup recovery is not wired to server initialization.", el("div", "notice warning-note", "Do not treat this page as proof that restart replays committed transactions or undoes in-flight transactions. Separate RecoveryManager crash tests passed, but integration with persistent table/catalog metadata remains unfinished."))]);
  }
  function renderBufferMini(metrics) {
    const root = document.getElementById("concurrent-buffer-view");
    if (!root) return;
    const b = metrics.buffer_pool || {};
    const capacity = Number(b.frame_capacity || 4);
    const resident = Number(b.resident_pages || 0);
    const pinned = Number(b.pinned_frames || 0);
    root.replaceChildren();
    const frames = el("div", "buffer-mini-frames");
    for (let i = 0; i < capacity; i += 1) {
      const item = el("div", `buffer-mini-frame ${i < resident ? "resident" : "free"}`);
      item.append(el("strong", "", `F${i}`), el("span", "", i < resident ? "Resident" : "Free"));
      frames.appendChild(item);
    }
    root.append(frames, el("div", "queue-numbers", `${resident}/${capacity} resident · ${pinned} pinned · ${capacity * 4096 / 1024} KB configured cache`));
  }

  async function refresh() {
    try {
      const m = await MiniDBApi.metrics();
      renderMemory(m); renderConcurrency(m); renderOS(m); renderStorage(m); renderRecovery(m); renderBufferMini(m);
      const worker = document.querySelector(".stat-grid .stat-card:nth-child(3) .stat-value");
      const frames = document.querySelector(".stat-grid .stat-card:nth-child(4) .stat-value");
      if (worker) worker.textContent = m.workers?.count ?? "—";
      if (frames) frames.textContent = m.buffer_pool?.frame_capacity ?? "—";
      const stamp = document.getElementById("last-updated");
      if (stamp) stamp.textContent = `Metrics ${new Date().toLocaleTimeString()}`;
      const pid = document.getElementById("top-pid");
      const workers = document.getElementById("top-workers");
      const buffer = document.getElementById("top-frames");
      const uptime = document.getElementById("top-uptime");
      if (pid) pid.textContent = `PID ${m.process_id ?? "—"}`;
      if (workers) workers.textContent = `Workers: ${m.workers?.count ?? "—"}`;
      if (buffer) buffer.textContent = `Buffer: ${m.buffer_pool?.frame_capacity ?? "—"}`;
      if (uptime) uptime.textContent = `Uptime ${Math.floor(Number(m.uptime_seconds || 0) / 60)}m ${Number(m.uptime_seconds || 0) % 60}s`;
    } catch (error) {
      ["memory-metrics", "concurrency-metrics", "os-metrics", "storage-metrics", "recovery-metrics"].forEach(id => {
        const root = document.getElementById(id);
        if (root) root.replaceChildren(el("div", "panel empty-state", `Metrics unavailable: ${error.message}`));
      });
    }
  }
  document.addEventListener("DOMContentLoaded", () => {
    refresh();
    document.addEventListener("minidb:metrics-updated", (event) => {
      const m = event.detail;
      if (m && m.concurrency) renderConcurrency(m);
      if (m && m.workers) { renderOS(m); renderMemory(m); renderStorage(m); renderBufferMini(m); }
    });
    window.setInterval(refresh, 1500);
    document.getElementById("refresh-btn")?.addEventListener("click", refresh);
    document.querySelectorAll(".nav-item, [data-go]").forEach(button => button.addEventListener("click", refresh));
  });
})();
