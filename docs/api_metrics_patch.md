# Patch required in the existing `src/server/api.cpp`

Keep the existing JSON parser and the existing SQL/session endpoint implementation.
Apply these two small changes.

## 1. Add this method after `SqlApi::SqlApi(...)`

```cpp
void SqlApi::SetMetricsProvider(MetricsProvider provider) {
    std::lock_guard<std::mutex> lock(metrics_mutex_);
    metrics_provider_ = std::move(provider);
}
```

`api.cpp` already needs `<mutex>` and `<utility>`; add them if missing.

## 2. Add this route inside `SqlApi::Handle`, inside its `try` block, after
`GET /api/health` and before the other fallback routes

```cpp
if (method == "GET" && path == "/api/metrics") {
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
```

The earlier `ErrorResponse` helper in `api.cpp` is reused. Do not add a second
JSON parser or copy metric values into `api.cpp`.
