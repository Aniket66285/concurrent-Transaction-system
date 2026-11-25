#!/usr/bin/env bash
set -euo pipefail
PROJECT_ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
cd "$PROJECT_ROOT"
mkdir -p build data
CXX="${CXX:-g++}"

SOURCES=(
  src/catalog/schema.cpp
  src/catalog/tuple.cpp
  src/catalog/catalog.cpp
  src/storage/disk_manager.cpp
  src/storage/slotted_page.cpp
  src/storage/table_heap.cpp
  src/buffer/lru_replacer.cpp
  src/buffer/buffer_pool.cpp
  src/buffer/page_guard.cpp
  src/recovery/log_record.cpp
  src/recovery/binary_log_record_codec.cpp
  src/recovery/log_manager.cpp
  src/concurrency/transaction.cpp
  src/concurrency/wait_for_graph.cpp
  src/concurrency/deadlock_detector.cpp
  src/concurrency/lock_manager.cpp
  src/transaction/txn_manager.cpp
  src/sql/lexer.cpp
  src/sql/parser.cpp
  src/sql/executor.cpp
  src/sql/session.cpp
  src/sql/sql_engine.cpp
  src/server/semaphore_gate.cpp
  src/server/worker_pool.cpp
  src/server/api.cpp
  src/server/http_server.cpp
  src/os/procfs_reader.cpp
  src/os/scheduler_experiment.cpp
  src/observability/runtime_metrics.cpp
  apps/server_main.cpp
)

echo "Building MiniDB Server..."
"$CXX" -std=c++17 -O2 -Wall -Wextra -Wpedantic -pthread -Iinclude \
  "${SOURCES[@]}" -o build/minidb_server

echo "Build successful. Starting MiniDB..."
exec ./build/minidb_server
