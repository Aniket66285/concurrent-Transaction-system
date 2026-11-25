#!/usr/bin/env bash
set -euo pipefail

BASE_URL="${MINIDB_API_URL:-http://127.0.0.1:8080}"
TABLE="lock_lab_$$"
B_PID=""
TMP_RESULT="$(mktemp)"
cleanup() {
  if [[ -n "$B_PID" ]]; then kill "$B_PID" 2>/dev/null || true; fi
  rm -f "$TMP_RESULT"
}
trap cleanup EXIT

new_session() {
  curl -fsS -X POST "$BASE_URL/api/sessions" \
    -H 'Content-Type: application/json' -d '{}' |
    python3 -c 'import json,sys; print(json.load(sys.stdin)["session_id"])'
}
run_sql() {
  local sid="$1" sql="$2"
  python3 -c 'import json,sys; print(json.dumps({"session_id":int(sys.argv[1]),"sql":sys.argv[2]}))' "$sid" "$sql" |
    curl -fsS --max-time 60 -X POST "$BASE_URL/api/sql" \
      -H 'Content-Type: application/json' -d @-
}

A="$(new_session)"
B="$(new_session)"
echo "Client A session=$A | Client B session=$B"
run_sql "$A" "CREATE TABLE $TABLE (id INT, balance INT);" >/dev/null
run_sql "$A" "INSERT INTO $TABLE VALUES (1, 1000);" >/dev/null
run_sql "$A" "BEGIN; UPDATE $TABLE SET balance = balance - 100 WHERE id = 1;" >/dev/null

# Client B is deliberately sent in the background; its request must block on A's lock.
(run_sql "$B" "BEGIN; UPDATE $TABLE SET balance = balance - 50 WHERE id = 1;" >"$TMP_RESULT") &
B_PID=$!
OBSERVED=0
for _ in $(seq 1 100); do
  METRICS="$(curl -fsS "$BASE_URL/api/metrics")"
  OBSERVED="$(python3 -c 'import json,sys; c=json.load(sys.stdin)["concurrency"]; print(1 if c.get("wait_for_edge_count",0)>0 and c.get("waiting_count",0)>0 else 0)' <<<"$METRICS")"
  if [[ "$OBSERVED" == "1" ]]; then
    python3 -c 'import json,sys; c=json.load(sys.stdin)["concurrency"]; assert c["lock_table"], c; assert any(x["waiters"] for x in c["lock_table"]), c; print("Live lock-table waiter and wait-for edge: PASS")' <<<"$METRICS"
    break
  fi
  sleep 0.05
done

if [[ "$OBSERVED" != "1" ]]; then
  echo "ERROR: No live lock wait observed. Check the server/API and SQL engine."
  exit 1
fi

run_sql "$A" 'COMMIT;' >/dev/null
wait "$B_PID"
B_PID=""
run_sql "$B" 'COMMIT;' >/dev/null
FINAL="$(run_sql "$A" "SELECT balance FROM $TABLE WHERE id = 1;")"
python3 -c 'import json,sys; d=json.load(sys.stdin); rows=d["results"][-1]["rows"]; assert rows and int(rows[0][0])==850, d; print("Two independent SQL clients overlapped and B resumed after A COMMIT: PASS")' <<<"$FINAL"
echo "Concurrency Lab integration test passed. Table $TABLE is retained in the current demo database."
