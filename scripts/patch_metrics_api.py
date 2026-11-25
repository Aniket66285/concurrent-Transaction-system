#!/usr/bin/env python3
from pathlib import Path

path = Path("src/server/api.cpp")
if not path.exists():
    raise SystemExit("Run this script from the MiniDB project root; src/server/api.cpp was not found.")
text = path.read_text()

# Add required includes without duplicating them.
if "#include <mutex>" not in text:
    text = text.replace("#include <map>", "#include <map>\n#include <mutex>", 1) if "#include <map>" in text else text.replace("#include <sstream>", "#include <mutex>\n#include <sstream>", 1)
if "#include <utility>" not in text:
    text = text.replace("#include <variant>", "#include <utility>\n#include <variant>", 1) if "#include <variant>" in text else text.replace("#include <string>", "#include <string>\n#include <utility>", 1)

ctor = "SqlApi::SqlApi(SqlEngine& engine)\n    : engine_(engine) {}"
setter = """SqlApi::SqlApi(SqlEngine& engine)
    : engine_(engine) {}

void SqlApi::SetMetricsProvider(MetricsProvider provider) {
    std::lock_guard<std::mutex> lock(metrics_mutex_);
    metrics_provider_ = std::move(provider);
}"""
if "void SqlApi::SetMetricsProvider" not in text:
    if ctor not in text:
        raise SystemExit("Could not find SqlApi constructor anchor. No source changes were written.")
    text = text.replace(ctor, setter, 1)

route_anchor = '        if (method == "OPTIONS") {'
route = '''        if (method == "GET" && path == "/api/metrics") {
            MetricsProvider provider;
            {
                std::lock_guard<std::mutex> lock(metrics_mutex_);
                provider = metrics_provider_;
            }
            if (!provider) {
                return ErrorResponse(503, "Runtime metrics provider is not configured");
            }
            return {200, provider()};
        }

'''
if 'path == "/api/metrics"' not in text:
    if route_anchor not in text:
        raise SystemExit("Could not find OPTIONS route anchor. No source changes were written.")
    text = text.replace(route_anchor, route + route_anchor, 1)

path.write_text(text)
print("Patched src/server/api.cpp with GET /api/metrics")
