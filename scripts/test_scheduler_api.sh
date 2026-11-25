#!/usr/bin/env bash
set -euo pipefail
BASE_URL="${MINIDB_API_URL:-http://127.0.0.1:8080}"
curl -fsS -X POST "$BASE_URL/api/experiments/scheduler/start" \
  -H 'Content-Type: application/json' -d '{"threads":3,"duration_ms":750}' >/dev/null
for _ in $(seq 1 100); do
  STATUS="$(curl -fsS "$BASE_URL/api/experiments/scheduler/status")"
  RUNNING="$(python3 -c 'import json,sys; print(json.load(sys.stdin)["running"])' <<<"$STATUS")"
  [[ "$RUNNING" == "False" ]] && break
  sleep 0.1
done
python3 -c '
import json,sys
x=json.loads(sys.stdin.read())
assert x["status"] == "completed", x
assert x["completed_threads"] == 3, x
assert len(x["results"]) == 3, x
assert len({r["tid"] for r in x["results"]}) == 3, x
assert all(r["elapsed_ms"] >= 700 for r in x["results"]), x
assert all(r["policy"] for r in x["results"]), x
print("3 distinct MiniDB WorkerPool Linux TIDs measured: PASS")
print("Linux scheduler policy and CPU/schedstat deltas: PASS")
for r in x["results"]:
    print("W{} TID={} policy={} CPUΔ={}ms runqueueΔ={}ms slicesΔ={}".format(r["worker_slot"]+1, r["tid"], r["policy"], r["cpu_time_delta_ms"], r["runqueue_wait_delta_ms"], r["timeslice_delta"]))
' <<<"$STATUS"
