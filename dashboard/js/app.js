(() => {
  "use strict";

  const $ = (selector, root = document) => root.querySelector(selector);
  const $$ = (selector, root = document) => Array.from(root.querySelectorAll(selector));
  const state = { sessions: [], sessionDetails: [], activeSessionId: null, connected: false, running: false };
  const pageNames = { overview: "Overview", workbench: "SQL Workbench", sessions: "Sessions", memory: "Buffer Pool", concurrency: "Concurrency Lab", os: "OS Scheduling Lab", storage: "Storage", recovery: "Recovery & WAL" };

  function toast(message, type = "success") {
    const item = document.createElement("div");
    item.className = `toast ${type}`;
    item.textContent = message;
    $("#toast-region").appendChild(item);
    setTimeout(() => item.remove(), 3800);
  }

  function setConnection(online, detail = "") {
    state.connected = online;
    ["#sidebar-status-dot", "#top-status-dot"].forEach((selector) => {
      const dot = $(selector);
      dot.classList.toggle("online", online);
      dot.classList.toggle("offline", !online);
    });
    $("#sidebar-status-text").textContent = online ? "Server connected" : "Server offline";
    $("#top-status-text").textContent = online ? "API Online" : "API Offline";
    $("#overview-server").textContent = online ? "Online" : "Offline";
    $("#overview-server-foot").textContent = detail || (online ? "Health endpoint responding" : "Cannot reach API endpoint");
  }

  function showPage(name) {
    if (!pageNames[name]) return;
    $$(".page").forEach((page) => page.classList.remove("active"));
    $(`#page-${name}`).classList.add("active");
    $$(".nav-item").forEach((item) => item.classList.toggle("active", item.dataset.page === name));
    $("#page-breadcrumb").textContent = pageNames[name];
    $("#sidebar").classList.remove("open");
    if (name === "sessions") refreshSessions();
    if (name === "workbench") refreshSessions().catch(() => {});
  }

  function setActiveSession(id) {
    state.activeSessionId = id == null ? null : Number(id);
    if (state.activeSessionId != null) {
      try { sessionStorage.setItem("minidb.activeSession", String(state.activeSessionId)); } catch (_) {}
    }
    renderSessionOptions();
    updateSessionLabels();
  }

  function updateSessionLabels() {
    const label = state.activeSessionId == null ? "No session selected" : `Using session #${state.activeSessionId}`;
    $("#quick-session-label").textContent = label;
  }

  function renderSessionOptions() {
    const select = $("#workbench-session");
    const previous = state.activeSessionId;
    select.replaceChildren();
    if (!state.sessions.length) {
      const option = document.createElement("option"); option.value = ""; option.textContent = "No session"; select.appendChild(option);
      state.activeSessionId = null;
    } else {
      state.sessions.forEach((id) => {
        const option = document.createElement("option");
        option.value = String(id); option.textContent = `Session #${id}`;
        select.appendChild(option);
      });
      const next = state.sessions.includes(Number(previous)) ? Number(previous) : state.sessions[0];
      state.activeSessionId = next;
      select.value = String(next);
    }
    updateSessionLabels();
  }

  function renderOverviewSessions() {
    const root = $("#overview-session-list");
    root.replaceChildren();
    if (!state.sessions.length) {
      const empty = document.createElement("div"); empty.className = "empty-state"; empty.textContent = "No active sessions. Create one to start querying."; root.appendChild(empty); return;
    }
    state.sessions.slice(0, 4).forEach((id) => {
      const row = document.createElement("div"); row.className = "compact-session";
      const avatar = document.createElement("div"); avatar.className = "session-avatar"; avatar.textContent = "S";
      const info = document.createElement("div"); info.className = "compact-session-info";
      const title = document.createElement("strong"); title.textContent = `SQL Client · Session #${id}`;
      const sub = document.createElement("small"); sub.textContent = state.activeSessionId === id ? "Selected session" : "Connected";
      info.append(title, sub);
      const status = document.createElement("span"); status.className = "session-live"; status.textContent = "● Active";
      row.append(avatar, info, status); root.appendChild(row);
    });
  }

  function renderSessionsTable() {
    const root = $("#sessions-table");
    root.replaceChildren();
    $("#session-page-summary").textContent = `${state.sessions.length} active SQL client session${state.sessions.length === 1 ? "" : "s"}.`;
    $("#sidebar-session-count").textContent = String(state.sessions.length);
    $("#overview-sessions").textContent = String(state.sessions.length);
    if (!state.sessions.length) {
      const empty = document.createElement("div"); empty.className = "empty-state large-empty";
      const strong = document.createElement("strong"); strong.textContent = "No active sessions";
      const sub = document.createElement("span"); sub.textContent = "Create a session to start sending SQL statements.";
      empty.append(strong, sub); root.appendChild(empty); return;
    }
    const details = new Map(state.sessionDetails.map(item => [Number(item.session_id), item]));
    const table = document.createElement("table"); table.className = "data-table";
    const thead = document.createElement("thead");
    const headRow = document.createElement("tr");
    ["SESSION ID", "CLIENT / CONTEXT", "TRANSACTION", "STATE", "ACTION"].forEach((text) => { const th = document.createElement("th"); th.textContent = text; headRow.appendChild(th); });
    thead.appendChild(headRow); table.appendChild(thead);
    const tbody = document.createElement("tbody");
    state.sessions.forEach((id) => {
      const info = details.get(Number(id)) || {};
      const tr = document.createElement("tr");
      const idCell = document.createElement("td"); idCell.className = "table-id"; idCell.textContent = `S${id}`;
      const clientCell = document.createElement("td");
      const clientName = document.createElement("strong"); clientName.textContent = `SQL client session #${id}`;
      const clientHelp = document.createElement("small"); clientHelp.className = "session-context-help"; clientHelp.textContent = "Owns its current transaction context";
      clientCell.append(clientName, document.createElement("br"), clientHelp);
      const txnCell = document.createElement("td");
      txnCell.textContent = info.has_transaction ? `T${info.transaction_id}` : "—";
      const statusCell = document.createElement("td");
      const status = document.createElement("span"); status.className = `status-label ${info.has_transaction ? "status-busy" : ""}`;
      status.textContent = info.transaction_state || "IDLE";
      if (info.session_busy) status.textContent = "BUSY / EXECUTING";
      statusCell.appendChild(status);
      const actionCell = document.createElement("td");
      const close = document.createElement("button"); close.className = "text-button"; close.textContent = "Close session"; close.dataset.closeSession = String(id);
      if (state.sessions.length === 1) close.title = "Closing the active session will require creating another one to run SQL";
      actionCell.appendChild(close); tr.append(idCell, clientCell, txnCell, statusCell, actionCell); tbody.appendChild(tr);
    });
    table.appendChild(tbody); root.appendChild(table);
  }

  async function refreshHealth() {
    try { const result = await MiniDBApi.health(); setConnection(true, result.status === "ok" ? "Health endpoint responding" : "API responded"); }
    catch (error) { setConnection(false, error.message); }
  }

  async function refreshSessions() {
    const data = await MiniDBApi.getSessions();
    state.sessions = Array.isArray(data.sessions) ? data.sessions.map(Number) : [];
    state.sessionDetails = Array.isArray(data.session_details) ? data.session_details : [];
    if (state.activeSessionId != null && !state.sessions.includes(state.activeSessionId)) state.activeSessionId = null;
    renderSessionOptions(); renderOverviewSessions(); renderSessionsTable();
    $("#last-updated").textContent = `Updated ${new Date().toLocaleTimeString()}`;
  }

  async function createSession() {
    const data = await MiniDBApi.createSession();
    await refreshSessions();
    setActiveSession(Number(data.session_id));
    toast(`Session #${data.session_id} created`);
    return data.session_id;
  }

  async function ensureSession() {
    if (state.activeSessionId != null && state.sessions.includes(state.activeSessionId)) return state.activeSessionId;
    if (!state.sessions.length) return await createSession();
    setActiveSession(state.sessions[0]);
    return state.activeSessionId;
  }

  async function closeSession(id) {
    await MiniDBApi.closeSession(id);
    if (state.activeSessionId === Number(id)) state.activeSessionId = null;
    await refreshSessions();
    toast(`Session #${id} closed`);
  }

  function updateLineNumbers() {
    const count = $("#sql-editor").value.split("\n").length;
    $("#line-numbers").textContent = Array.from({ length: count }, (_, i) => i + 1).join("\n");
  }

  function renderQueryResults(results, target = "#query-results") {
    const root = $(target); root.replaceChildren();
    let totalRows = 0;
    results.forEach((result, index) => {
      const message = document.createElement("div"); message.className = "result-message";
      message.textContent = `${result.message || "Statement completed"} · ${Number(result.rows_affected || 0)} row(s) affected`;
      root.appendChild(message);
      const columns = Array.isArray(result.columns) ? result.columns : [];
      const rows = Array.isArray(result.rows) ? result.rows : [];
      totalRows += rows.length;
      if (columns.length) {
        const table = document.createElement("table"); table.className = "result-table";
        const thead = document.createElement("thead"); const headRow = document.createElement("tr");
        columns.forEach((column) => { const th = document.createElement("th"); th.textContent = String(column); headRow.appendChild(th); });
        thead.appendChild(headRow); table.appendChild(thead);
        const tbody = document.createElement("tbody");
        rows.forEach((row) => {
          const tr = document.createElement("tr");
          row.forEach((value) => { const td = document.createElement("td"); td.textContent = value === null ? "NULL" : String(value); tr.appendChild(td); });
          tbody.appendChild(tr);
        });
        table.appendChild(tbody); root.appendChild(table);
      }
      if (index < results.length - 1) { const divider = document.createElement("div"); divider.style.height = "8px"; root.appendChild(divider); }
    });
    if (!results.length) root.textContent = "No statements returned results.";
    $("#result-summary").textContent = `${results.length} statement(s) executed · ${totalRows} result row(s)`;
  }

  async function executeSql(sql, compact = false) {
    if (state.running) return;
    if (!sql.trim()) { toast("Write a SQL statement first", "error"); return; }
    state.running = true;
    const runButton = compact ? $("#quick-run") : $("#run-sql");
    const oldText = runButton.textContent; runButton.disabled = true; runButton.textContent = "Running…";
    if (!compact) { $("#result-state").textContent = "RUNNING"; $("#result-state").className = "result-state"; }
    try {
      const sessionId = await ensureSession();
      const data = await MiniDBApi.executeSql(sessionId, sql);
      const results = Array.isArray(data.results) ? data.results : [];
      if (compact) {
        $("#quick-result").classList.remove("hidden");
        $("#quick-result").textContent = results.map((r) => r.message || "Statement completed").join(" · ");
      } else {
        renderQueryResults(results);
        $("#result-state").textContent = "SUCCESS"; $("#result-state").className = "result-state success";
      }
      toast("SQL executed successfully");
      await refreshSessions().catch(() => {});
    } catch (error) {
      if (compact) {
        $("#quick-result").classList.remove("hidden"); $("#quick-result").textContent = error.message; $("#quick-result").style.color = "#ff9aaa";
      } else {
        const root = $("#query-results"); root.replaceChildren(); const message = document.createElement("div"); message.className = "result-message"; message.style.color = "#ff9aaa"; message.textContent = error.message; root.appendChild(message);
        $("#result-summary").textContent = "Execution failed"; $("#result-state").textContent = "ERROR"; $("#result-state").className = "result-state error";
      }
      toast(error.message, "error");
    } finally {
      state.running = false; runButton.disabled = false; runButton.textContent = oldText;
    }
  }

  function bindEvents() {
    $$(".nav-item").forEach((item) => item.addEventListener("click", () => showPage(item.dataset.page)));
    $$('[data-go]').forEach((item) => item.addEventListener("click", () => showPage(item.dataset.go)));
    $("#mobile-menu").addEventListener("click", () => $("#sidebar").classList.toggle("open"));
    $("#refresh-btn").addEventListener("click", refreshAll);
    $("#reconnect-btn").addEventListener("click", refreshAll);
    $("#workbench-session").addEventListener("change", (event) => setActiveSession(Number(event.target.value)));
    $("#create-session").addEventListener("click", () => createSession().catch((e) => toast(e.message, "error")));
    $("#workbench-new-session").addEventListener("click", () => createSession().catch((e) => toast(e.message, "error")));
    $("#overview-new-session").addEventListener("click", () => createSession().catch((e) => toast(e.message, "error")));
    $("#refresh-sessions").addEventListener("click", () => refreshSessions().then(() => toast("Sessions refreshed")).catch((e) => toast(e.message, "error")));
    $("#sessions-table").addEventListener("click", (event) => {
      const button = event.target.closest("[data-close-session]");
      if (button) closeSession(Number(button.dataset.closeSession)).catch((e) => toast(e.message, "error"));
    });
    $("#run-sql").addEventListener("click", () => executeSql($("#sql-editor").value));
    $("#quick-run").addEventListener("click", () => executeSql($("#quick-sql").value, true));
    $("#sql-editor").addEventListener("input", updateLineNumbers);
    $("#sql-editor").addEventListener("keydown", (event) => {
      if ((event.ctrlKey || event.metaKey) && event.key === "Enter") { event.preventDefault(); executeSql($("#sql-editor").value); }
      if (event.key === "Tab") { event.preventDefault(); const start = event.target.selectionStart; const end = event.target.selectionEnd; event.target.setRangeText("  ", start, end, "end"); }
    });
    $("#clear-sql").addEventListener("click", () => { $("#sql-editor").value = ""; updateLineNumbers(); $("#sql-editor").focus(); });
    $("#format-sql").addEventListener("click", () => {
      const keywords = "SELECT|FROM|WHERE|INSERT INTO|VALUES|UPDATE|SET|DELETE FROM|CREATE TABLE|DROP TABLE|ALTER TABLE|ADD COLUMN|ORDER BY|GROUP BY|HAVING|LIMIT|OFFSET|JOIN|INNER JOIN|LEFT JOIN|RIGHT JOIN|ON|AND|OR|NOT|NULL|IS NULL|IS NOT NULL|BEGIN|COMMIT|ROLLBACK|ASC|DESC|AS|DISTINCT";
      const pattern = new RegExp(`\\b(${keywords})\\b`, "gi");
      $("#sql-editor").value = $("#sql-editor").value.replace(pattern, (match) => match.toUpperCase()); updateLineNumbers();
    });
    $$(".example-item").forEach((item) => item.addEventListener("click", () => { $("#sql-editor").value = item.dataset.query; updateLineNumbers(); $("#sql-editor").focus(); showPage("workbench"); }));
  }

  async function refreshAll() {
    await refreshHealth();
    try { await refreshSessions(); }
    catch (error) { toast(error.message, "error"); }
  }

  async function init() {
    bindEvents(); updateLineNumbers();
    try {
      const saved = Number(sessionStorage.getItem("minidb.activeSession"));
      if (Number.isSafeInteger(saved) && saved > 0) state.activeSessionId = saved;
    } catch (_) {}
    await refreshHealth();
    try { await refreshSessions(); }
    catch (error) { toast(error.message, "error"); }
    if (!state.sessions.length && state.connected) {
      try { await createSession(); }
      catch (error) { toast(`Could not create initial session: ${error.message}`, "error"); }
    }
    setInterval(async () => { await refreshHealth(); try { await refreshSessions(); } catch (_) {} }, 15000);
  }

  document.addEventListener("DOMContentLoaded", init);
})();
