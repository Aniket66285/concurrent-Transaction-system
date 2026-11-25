#!/usr/bin/env bash
set -euo pipefail
ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
cd "$ROOT"
command -v python3 >/dev/null 2>&1 || { echo 'python3 is required'; exit 1; }
echo 'MiniDB dashboard: http://127.0.0.1:5500'
echo 'Keep the MiniDB backend running separately on port 8080.'
exec python3 -m http.server 5500 --directory dashboard
