#!/usr/bin/env bash
set -euo pipefail
ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
cd "$ROOT"
mkdir -p build
CXX="${CXX:-g++}"
"$CXX" -std=c++17 -O2 -Wall -Wextra -Wpedantic -pthread \
  apps/client_main.cpp -o build/minidb_client
exec ./build/minidb_client "$@"
