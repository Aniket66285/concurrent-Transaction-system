(() => {
  "use strict";
  const $ = (selector) => document.querySelector(selector);
  let polling = false;

  function element(tag, cls, text) {
    const node = document.createElement(tag);
    if (cls) node.className = cls;
    if (text !== undefined) node.textContent = String(text);
    return node;
  }

  function renderSnapshot(snapshot) {
    const root = $("#scheduler-experiment-result");
    if (!root) return;
    root.replaceChildren();
    if (snapshot.error) root.appendChild(element("div", "notice warning-note", snapshot.error));
    if (!Array.isArray(snapshot.results) || !snapshot.results.length) {
      root.appendChild(element("div", "empty-state", snapshot.running
        ? `Experiment running: ${snapshot.completed_threads}/${snapshot.requested_threads} worker tasks complete…`
        : "No completed measurement yet."));
      return;
    }

    const summary = element("div", "scheduler-summary");
    summary.append(
      element("strong", "", `Job #${snapshot.job_id} · ${snapshot.status.toUpperCase()}`),
      element("span", "", `${snapshot.completed_threads}/${snapshot.requested_threads} worker(s) · requested ${snapshot.duration_ms} ms CPU workload`)
    );
    root.appendChild(summary);

    const wrap = element("div", "metrics-table-wrap");
    const table = element("table", "metrics-table scheduler-table");
    const thead = element("thead");
    const head = element("tr");
    ["WORKER", "LINUX TID", "POLICY", "NICE / PRIORITY", "CPU BEFORE → AFTER", "CPU TIME Δ", "RUN-QUEUE WAIT Δ", "TIMESLICES Δ", "VOL / INVOL CS Δ"].forEach((h) => head.appendChild(element("th", "", h)));
    thead.appendChild(head);
    const tbody = element("tbody");
    snapshot.results.forEach((r) => {
      const tr = element("tr");
      [
        `W${Number(r.worker_slot) + 1}`,
        r.tid,
        r.policy,
        r.priority,
        `${r.last_cpu_before} → ${r.last_cpu_after}`,
        `${r.cpu_time_delta_ms} ms`,
        r.schedstat_readable ? `${r.runqueue_wait_delta_ms} ms` : "schedstat unavailable",
        r.schedstat_readable ? r.timeslice_delta : "—",
        `${r.voluntary_context_switch_delta} / ${r.involuntary_context_switch_delta}`
      ].forEach((value) => tr.appendChild(element("td", "", value)));
      tbody.appendChild(tr);
    });
    table.append(thead, tbody);
    wrap.appendChild(table);
    root.appendChild(wrap);

    root.appendChild(element("div", "notice", "CPU time Δ and scheduler runtime Δ are cumulative counter differences during this experiment. Run-queue wait Δ measures time ready but waiting for CPU. Timeslices Δ approximates how many scheduler slices were used. Results depend on the host kernel, core count and other system load."));
  }

  async function refreshStatus() {
    const status = await MiniDBApi.schedulerExperimentStatus();
    renderSnapshot(status);
    const badge = $("#scheduler-status");
    badge.textContent = status.running ? "RUNNING" : String(status.status || "IDLE").toUpperCase();
    badge.className = `result-state ${status.running ? "running" : status.status === "completed" ? "success" : ""}`;
    return status;
  }

  async function startExperiment() {
    if (polling) return;
    const button = $("#scheduler-start");
    const count = Number($("#scheduler-thread-count").value);
    const duration = Number($("#scheduler-duration").value);
    polling = true;
    button.disabled = true;
    button.textContent = "Starting…";
    $("#scheduler-status").textContent = "STARTING";
    try {
      await MiniDBApi.startSchedulerExperiment(count, duration);
      button.textContent = "Experiment running…";
      let status;
      do {
        await new Promise((resolve) => window.setTimeout(resolve, 500));
        status = await refreshStatus();
      } while (status.running);
      button.textContent = "▶ Start CPU scheduling experiment";
    } catch (error) {
      $("#scheduler-experiment-result").replaceChildren(element("div", "notice warning-note", error.message));
      $("#scheduler-status").textContent = "ERROR";
      button.textContent = "▶ Start CPU scheduling experiment";
    } finally {
      polling = false;
      button.disabled = false;
    }
  }

  document.addEventListener("DOMContentLoaded", () => {
    $("#scheduler-start")?.addEventListener("click", startExperiment);
    refreshStatus().catch(() => {});
  });
})();
