#!/usr/bin/env bash
set -euo pipefail
ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
cd "$ROOT"
mkdir -p build

g++ -std=c++17 -Wall -Wextra -Wpedantic -pthread -Iinclude \
  src/server/semaphore_gate.cpp src/server/worker_pool.cpp \
  tests/worker_pool_metrics_test.cpp -o build/worker_pool_metrics_test
./build/worker_pool_metrics_test

g++ -std=c++17 -Wall -Wextra -Wpedantic -Iinclude \
  src/os/procfs_reader.cpp tests/procfs_reader_test.cpp \
  -o build/procfs_reader_test
./build/procfs_reader_test

g++ -std=c++17 -Wall -Wextra -Wpedantic -pthread -Iinclude \
  src/server/semaphore_gate.cpp src/server/worker_pool.cpp \
  src/os/procfs_reader.cpp src/os/scheduler_experiment.cpp \
  tests/scheduler_experiment_test.cpp -o build/scheduler_experiment_test
./build/scheduler_experiment_test

node --check dashboard/js/api.js
node --check dashboard/js/app.js
node --check dashboard/js/monitoring.js
node --check dashboard/js/concurrent_lab.js
node --check dashboard/js/scheduler_lab.js
node --check dashboard/js/cpu_scheduling.js
node --check dashboard/js/cpu_scheduling_ui.js
node tests/cpu_scheduling_test.cjs
python3 -m py_compile scripts/patch_metrics_api.py

echo 'Telemetry focused checks passed.'
