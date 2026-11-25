(() => {
  "use strict";
  const API_BASE = window.MINIDB_API_BASE || "http://127.0.0.1:8080";
  async function request(path, options = {}) {
    const controller = new AbortController();
    const { timeoutMs = 15000, ...fetchOptions } = options;
    const timeout = setTimeout(() => controller.abort(), timeoutMs);
    try {
      const response = await fetch(`${API_BASE}${path}`, {
        ...fetchOptions,
        signal: controller.signal,
        headers: { ...(fetchOptions.body ? { "Content-Type": "application/json" } : {}), ...(fetchOptions.headers || {}) }
      });
      const raw = await response.text();
      let data = {};
      if (raw) {
        try { data = JSON.parse(raw); }
        catch { throw new Error(`Server returned invalid JSON (HTTP ${response.status})`); }
      }
      if (!response.ok) throw new Error(data.error || `HTTP ${response.status}`);
      return data;
    } catch (error) {
      if (error.name === "AbortError") throw new Error("Request timed out. Check whether MiniDB is running.");
      if (error instanceof TypeError) throw new Error("Cannot reach MiniDB API at " + API_BASE);
      throw error;
    } finally { clearTimeout(timeout); }
  }
  window.MiniDBApi = {
    baseUrl: API_BASE,
    health: () => request("/api/health"),
    metrics: () => request("/api/metrics"),
    getSessions: () => request("/api/sessions"),
    createSession: () => request("/api/sessions", { method: "POST", body: "{}" }),
    closeSession: (id) => request(`/api/sessions/${encodeURIComponent(id)}`, { method: "DELETE" }),
    executeSql: (sessionId, sql, timeoutMs = 120000) => request("/api/sql", { method: "POST", body: JSON.stringify({ session_id: Number(sessionId), sql }), timeoutMs }),
    startSchedulerExperiment: (threads, durationMs) => request("/api/experiments/scheduler/start", { method: "POST", body: JSON.stringify({ threads, duration_ms: durationMs }) }),
    schedulerExperimentStatus: () => request("/api/experiments/scheduler/status")
  };
})();
