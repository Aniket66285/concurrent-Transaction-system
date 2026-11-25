#!/usr/bin/env bash
set -euo pipefail
ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
cd "$ROOT"
command -v node >/dev/null 2>&1 || { echo 'Node.js 18+ is required for this test.' >&2; exit 1; }
node --check dashboard/js/cpu_scheduling.js
node --check dashboard/js/cpu_scheduling_ui.js
node tests/cpu_scheduling_test.cjs
