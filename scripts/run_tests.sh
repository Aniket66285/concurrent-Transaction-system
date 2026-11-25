#!/usr/bin/env bash
set -euo pipefail
ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
cd "$ROOT"

if [[ ! -f build/CTestTestfile.cmake ]]; then
  ./scripts/build.sh
fi

ctest --test-dir build --output-on-failure
./scripts/test_telemetry.sh

echo "Unit and telemetry tests completed. For live two-client + scheduler API integration, run ./scripts/verify.sh."
