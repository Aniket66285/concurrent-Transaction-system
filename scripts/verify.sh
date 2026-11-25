#!/usr/bin/env bash
set -euo pipefail
ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
cd "$ROOT"

PORT="${MINIDB_VERIFY_PORT:-18882}"
BASE_URL="http://127.0.0.1:${PORT}"
LOG_FILE="${TMPDIR:-/tmp}/minidb-verify-${PORT}.log"
SERVER_PID=""

cleanup() {
  if [[ -n "$SERVER_PID" ]]; then
    kill "$SERVER_PID" 2>/dev/null || true
    wait "$SERVER_PID" 2>/dev/null || true
  fi
}
trap cleanup EXIT INT TERM

if curl -fsS "$BASE_URL/api/health" >/dev/null 2>&1; then
  echo "Port $PORT already has a MiniDB-compatible API. Choose another MINIDB_VERIFY_PORT." >&2
  exit 1
fi

echo "[1/5] CMake build"
./scripts/build.sh

echo "[2/5] CTest"
ctest --test-dir build --output-on-failure

echo "[3/5] WorkerPool / procfs / scheduler / JS checks"
./scripts/test_telemetry.sh

echo "[4/5] Starting isolated verification server on port $PORT"
MINIDB_PORT="$PORT" ./build/minidb_server >"$LOG_FILE" 2>&1 &
SERVER_PID=$!
for _ in $(seq 1 100); do
  if curl -fsS "$BASE_URL/api/health" >/dev/null 2>&1; then
    break
  fi
  if ! kill -0 "$SERVER_PID" 2>/dev/null; then
    cat "$LOG_FILE" >&2
    echo "Verification server exited unexpectedly." >&2
    exit 1
  fi
  sleep 0.1
done
curl -fsS "$BASE_URL/api/health" >/dev/null

export MINIDB_API_URL="$BASE_URL"
echo "[5/5] Two-session lock-wait + Linux scheduler API integration"
./scripts/test_concurrency_lab.sh
./scripts/test_scheduler_api.sh

echo ""
echo "MiniDB verification completed successfully."
echo "Server log: $LOG_FILE"
