(() => {
  "use strict";

  const $ = (selector) => document.querySelector(selector);
  const clients = {
    A: { sessionId: null, running: false },
    B: { sessionId: null, running: false }
  };
  const timeline = [];
  let lastWaitEdgeSignature = "";
  const DEMO_SETUP = "CREATE TABLE accounts (id INT, balance INT);\nINSERT INTO accounts VALUES (1, 1000);";


  function logEvent(message, type = "info") {
    timeline.unshift({ time: new Date().toLocaleTimeString(), message, type });
    if (timeline.length > 40) timeline.length = 40;
    renderTimeline();
  }

  function renderTimeline() {
    const root = $("#concurrent-event-timeline");
    if (!root) return;
    root.replaceChildren();
    if (!timeline.length) {
      root.appendChild(document.createElement("div"));
      root.firstChild.className = "empty-state";
      root.firstChild.textContent = "Waiting for client activity…";
      return;
    }
    timeline.forEach((event) => {
      const row = document.createElement("div");
      row.className = `event-row ${event.type}`;
      const time = document.createElement("time");
      time.textContent = event.time;
      const marker = document.createElement("span");
      marker.className = "event-marker";
      const text = document.createElement("span");
      text.textContent = event.message;
      row.append(time, marker, text);
      root.appendChild(row);
    });
  }

  function setClientState(name, state, detail) {
    const root = $(`#client-${name.toLowerCase()}-state`);
    if (!root) return;
    root.textContent = state;
    root.className = `client-state ${state.toLowerCase()}`;
    if (detail) root.title = detail;
  }

  function showClientOutput(name, message, type = "info", results) {
    const root = $(`#client-${name.toLowerCase()}-result`);
    if (!root) return;
    root.replaceChildren();
    const line = document.createElement("div");
    line.className = `client-output-line ${type}`;
    line.textContent = message;
    root.appendChild(line);
    if (Array.isArray(results)) {
      results.forEach((result) => {
        const summary = document.createElement("div");
        summary.className = "client-output-summary";
        summary.textContent = `${result.message || "Statement complete"} · ${Number(result.rows_affected || 0)} row(s) affected`;
        root.appendChild(summary);
        const columns = Array.isArray(result.columns) ? result.columns : [];
        const rows = Array.isArray(result.rows) ? result.rows : [];
        if (columns.length) {
          const table = document.createElement("table");
          table.className = "result-table concurrent-result-table";
          const thead = document.createElement("thead");
          const trHead = document.createElement("tr");
          columns.forEach((column) => {
            const th = document.createElement("th");
            th.textContent = String(column);
            trHead.appendChild(th);
          });
          thead.appendChild(trHead);
          const tbody = document.createElement("tbody");
          rows.forEach((row) => {
            const tr = document.createElement("tr");
            row.forEach((value) => {
              const td = document.createElement("td");
              td.textContent = value === null ? "NULL" : String(value);
              tr.appendChild(td);
            });
            tbody.appendChild(tr);
          });
          table.append(thead, tbody);
          root.appendChild(table);
        }
      });
    }
  }

  async function getOrCreateSession(name, existingIds) {
    let saved = null;
    try { saved = Number(sessionStorage.getItem(`minidb.concurrent.${name}`)); } catch (_) {}
    if (Number.isSafeInteger(saved) && saved > 0 && existingIds.includes(saved)) {
      return saved;
    }
    const created = await MiniDBApi.createSession();
    const id = Number(created.session_id);
    try { sessionStorage.setItem(`minidb.concurrent.${name}`, String(id)); } catch (_) {}
    return id;
  }

  async function connectClients() {
    const data = await MiniDBApi.getSessions();
    const existing = Array.isArray(data.sessions) ? data.sessions.map(Number) : [];
    const a = await getOrCreateSession("A", existing);
    let b = await getOrCreateSession("B", [...existing, a]);
    if (a === b) {
      const replacement = await MiniDBApi.createSession();
      b = Number(replacement.session_id);
      try { sessionStorage.setItem("minidb.concurrent.B", String(b)); } catch (_) {}
    }
    clients.A.sessionId = a;
    clients.B.sessionId = b;
    $("#client-a-session").textContent = `Session S${a} · independent transaction context`;
    $("#client-b-session").textContent = `Session S${b} · independent transaction context`;
    $("#concurrent-session-summary").textContent = `Connected: Client A / Session ${a} · Client B / Session ${b}`;
    setClientState("A", "CONNECTED");
    setClientState("B", "CONNECTED");
    logEvent(`Independent SQL sessions connected: Client A S${a}, Client B S${b}.`, "success");
    return { a, b };
  }

  async function ensureConnected() {
    if (clients.A.sessionId && clients.B.sessionId && clients.A.sessionId !== clients.B.sessionId) {
      return;
    }
    await connectClients();
  }

  async function runClient(name, suppliedSql) {
    const client = clients[name];
    const editor = $(`#client-${name.toLowerCase()}-sql`);
    const button = $(`#client-${name.toLowerCase()}-run`);
    const sql = suppliedSql !== undefined ? suppliedSql : editor.value;
    if (!sql.trim()) {
      showClientOutput(name, "Write a SQL statement first.", "error");
      return;
    }
    if (client.running) {
      showClientOutput(name, "This client already has a request in progress.", "warning");
      return;
    }

    client.running = true;
    button.disabled = true;
    button.textContent = "Running…";
    setClientState(name, "RUNNING");
    logEvent(`Client ${name} sent SQL to session S${client.sessionId || "?"}.`, "pending");
    showClientOutput(name, "Request sent to MiniDB WorkerPool…", "pending");
    const started = performance.now();

    try {
      await ensureConnected();
      const response = await MiniDBApi.executeSql(client.sessionId, sql, 120000);
      const elapsed = (performance.now() - started).toFixed(1);
      showClientOutput(name, `SUCCESS · ${elapsed} ms · Session ${client.sessionId}`, "success", response.results || []);
      setClientState(name, "COMPLETED");
      logEvent(`Client ${name} request completed in ${elapsed} ms.`, "success");
    } catch (error) {
      const text = error.message || "SQL request failed";
      showClientOutput(name, `ERROR · ${text}`, "error");
      setClientState(name, "ERROR", text);
      logEvent(`Client ${name} request failed: ${text}`, "error");
    } finally {
      client.running = false;
      button.disabled = false;
      button.textContent = `▶ Run ${name}`;
      refreshLivePanels();
      window.dispatchEvent(new CustomEvent("minidb:sessions-changed"));
    }
    return !client.running && !document.querySelector(`#client-${name.toLowerCase()}-state`)?.classList.contains("error");
  }

  function renderWorkerList(metrics) {
    const root = $("#concurrent-worker-list");
    if (!root) return;
    root.replaceChildren();
    const workers = metrics?.workers || {};
    const threads = Array.isArray(workers.threads) ? workers.threads : [];
    threads.forEach((thread, index) => {
      const row = document.createElement("div");
      row.className = "worker-mini-row";
      const dot = document.createElement("span");
      dot.className = `worker-dot ${thread.busy ? "busy" : "idle"}`;
      const name = document.createElement("strong");
      name.textContent = `W${index + 1}`;
      const status = document.createElement("span");
      status.textContent = thread.busy ? "BUSY · executing request/task" : "IDLE · waiting for work";
      const tid = document.createElement("code");
      tid.textContent = `TID ${thread.tid}`;
      row.append(dot, name, status, tid);
      root.appendChild(row);
    });
    if (!threads.length) root.textContent = "Worker thread data unavailable.";
  }

  function renderQueue(metrics) {
    const root = $("#concurrent-queue-view");
    if (!root) return;
    root.replaceChildren();
    const workers = metrics?.workers || {};
    const capacity = Number(workers.queue_capacity || 8);
    const pending = Number(workers.queue_pending || 0);
    const active = Number(workers.active_tasks || 0);
    const bar = document.createElement("div");
    bar.className = "queue-slots";
    for (let i = 0; i < capacity; i += 1) {
      const slot = document.createElement("span");
      slot.className = `queue-slot ${i < pending ? "occupied" : "free"}`;
      slot.title = i < pending ? "Pending request slot" : "Available queue slot";
      bar.appendChild(slot);
    }
    const numbers = document.createElement("div");
    numbers.className = "queue-numbers";
    numbers.textContent = `${pending} pending / ${capacity} slots · ${active} worker task(s) active`;
    root.append(bar, numbers);
  }

  async function refreshLivePanels() {
    try {
      const metrics = await MiniDBApi.metrics();
      renderWorkerList(metrics);
      renderQueue(metrics);
      document.dispatchEvent(new CustomEvent("minidb:metrics-updated", { detail: metrics }));
      const edges = metrics?.concurrency?.wait_for_edges || [];
      const signature = JSON.stringify(edges);
      if (signature !== lastWaitEdgeSignature) {
        if (edges.length) {
          edges.forEach((edge) => logEvent(`Lock wait observed: T${edge.from_txn} waits for T${edge.to_txn}.`, "warning"));
        } else if (lastWaitEdgeSignature && lastWaitEdgeSignature !== "[]") {
          logEvent("Wait-for graph is clear; no transaction is currently waiting on a lock.", "success");
        }
        lastWaitEdgeSignature = signature;
      }
    } catch (_) {
      const root = $("#concurrent-worker-list");
      if (root && !root.children.length) root.textContent = "MiniDB metrics endpoint unavailable.";
    }
  }

  async function startGuidedLockWaitDemo() {
    const trigger = $("#concurrent-guided-demo");
    if (trigger) { trigger.disabled = true; trigger.textContent = "Preparing lock demo…"; }
    try {
      await ensureConnected();

      // Create the demo table once. If it exists, ensure row id=1 is present.
      let accountRows = null;
      try {
        const existing = await MiniDBApi.executeSql(
          clients.A.sessionId, "SELECT * FROM accounts WHERE id = 1;"
        );
        accountRows = existing.results?.[0]?.rows || [];
        if (!accountRows.length) {
          await MiniDBApi.executeSql(
            clients.A.sessionId, "INSERT INTO accounts VALUES (1, 1000);"
          );
          logEvent("Inserted missing demo row (id=1, balance=1000).", "success");
        } else {
          logEvent("Reusing existing accounts row id=1; current balance will be reduced by both clients.", "info");
        }
      } catch (lookupError) {
        await MiniDBApi.executeSql(
          clients.A.sessionId, "CREATE TABLE accounts (id INT, balance INT);"
        );
        await MiniDBApi.executeSql(
          clients.A.sessionId, "INSERT INTO accounts VALUES (1, 1000);"
        );
        logEvent("Created accounts table and inserted demo row (id=1, balance=1000).", "success");
      }

      const sqlA = "BEGIN;\nUPDATE accounts SET balance = balance - 100 WHERE id = 1;";
      const sqlB = "BEGIN;\nUPDATE accounts SET balance = balance - 50 WHERE id = 1;";
      $("#client-a-sql").value = sqlA;
      $("#client-b-sql").value = sqlB;

      logEvent("Step 1/3: Client A starts and acquires an exclusive lock on the matching RID.", "pending");
      const aOk = await runClient("A", sqlA);
      if (!aOk) throw new Error("Client A did not acquire its lock; inspect the Client A result.");

      logEvent("Step 2/3: Client B is now sent the same UPDATE. Its request should block on A's lock.", "pending");
      const bPromise = runClient("B", sqlB); // Intentionally not awaited while it waits.

      let observed = false;
      for (let attempt = 0; attempt < 30; attempt += 1) {
        await new Promise(resolve => window.setTimeout(resolve, 150));
        const metrics = await MiniDBApi.metrics();
        document.dispatchEvent(new CustomEvent("minidb:metrics-updated", { detail: metrics }));
        const edges = metrics?.concurrency?.wait_for_edges || [];
        if (edges.length > 0) {
          observed = true;
          edges.forEach(edge => logEvent(`LIVE GRAPH: T${edge.from_txn} → T${edge.to_txn} (waiter → lock holder).`, "warning"));
          break;
        }
      }

      if (observed) {
        showClientOutput("B", "WAITING · Client B is blocked by Client A's row lock. Keep A open, inspect the graph, then click Commit on Client A.", "warning");
        setClientState("B", "WAITING", "Waiting for Client A to release the RID lock");
        logEvent("Step 3/3: Wait-for edge is visible. Commit Client A to release the lock and allow B to continue.", "success");
      } else {
        logEvent("No wait edge was observed within 4.5 seconds. Refresh live state and check that both statements target the same existing row.", "warning");
      }

      // Keep a reference so the UI can report when B completes after A commits.
      bPromise.then(() => refreshLivePanels()).catch(() => refreshLivePanels());
    } catch (error) {
      logEvent(`Guided demo error: ${error.message}`, "error");
      showClientOutput("A", error.message, "error");
    } finally {
      if (trigger) { trigger.disabled = false; trigger.textContent = "⚡ Start guided lock-wait demo"; }
      await refreshLivePanels();
    }
  }

  function bindEvents() {
    $("#concurrent-create-sessions")?.addEventListener("click", () => connectClients().catch((e) => {
      $("#concurrent-session-summary").textContent = e.message;
    }));
    $("#concurrent-guided-demo")?.addEventListener("click", startGuidedLockWaitDemo);
    $("#concurrent-setup-demo")?.addEventListener("click", async () => {
      try {
        await ensureConnected();
        const succeeded = await runClient("A", DEMO_SETUP);
        if (!succeeded) {
          logEvent("Demo setup did not complete; accounts may already exist. No client editor was changed.", "warning");
          return;
        }
        $("#client-a-sql").value = "BEGIN;\nUPDATE accounts SET balance = balance - 100 WHERE id = 1;";
        $("#client-b-sql").value = "BEGIN;\nUPDATE accounts SET balance = balance - 50 WHERE id = 1;";
      } catch (error) {
        showClientOutput("A", `Setup failed: ${error.message}. Table may already exist.`, "error");
      }
    });
    $("#client-a-run")?.addEventListener("click", () => runClient("A"));
    $("#client-b-run")?.addEventListener("click", () => runClient("B"));
    $("#concurrent-run-both")?.addEventListener("click", async () => {
      try {
        await ensureConnected();
        // Both fetches are initiated without awaiting either one first.
        await Promise.allSettled([runClient("A"), runClient("B")]);
      } catch (error) {
        $("#concurrent-session-summary").textContent = error.message;
      }
    });
    $("#concurrent-refresh")?.addEventListener("click", refreshLivePanels);
    $("#concurrent-clear-timeline")?.addEventListener("click", () => { timeline.length = 0; renderTimeline(); });
    document.querySelectorAll("[data-client-command]").forEach((button) => {
      button.addEventListener("click", () => {
        const name = button.dataset.clientCommand;
        const command = button.dataset.command;
        if (command === "CLEAR") {
          $(`#client-${name.toLowerCase()}-sql`).value = "";
          return;
        }
        $(`#client-${name.toLowerCase()}-sql`).value = `${command};`;
        runClient(name, `${command};`);
      });
    });
  }

  document.addEventListener("DOMContentLoaded", () => {
    bindEvents();
    connectClients().catch((error) => {
      $("#concurrent-session-summary").textContent = `Connection error: ${error.message}`;
    });
    refreshLivePanels();
    window.setInterval(refreshLivePanels, 1000);
  });
})();
