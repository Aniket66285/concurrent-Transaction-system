#pragma once

#include "minidb/sql/sql_engine.hpp"

#include <functional>
#include <mutex>
#include <string>

namespace minidb {

class SchedulerExperiment;

struct ApiResponse {
    int status_code{200};
    std::string body;
};

class SqlApi {
public:
    using MetricsProvider = std::function<std::string()>;

    explicit SqlApi(SqlEngine& engine);

    // Install before HttpServer::Start. The provider must return valid JSON.
    void SetMetricsProvider(MetricsProvider provider);
    void SetSchedulerExperiment(SchedulerExperiment* experiment);

    ApiResponse Handle(
        const std::string& method,
        const std::string& path,
        const std::string& body
    );

private:
    SqlEngine& engine_;
    mutable std::mutex metrics_mutex_;
    MetricsProvider metrics_provider_;
    SchedulerExperiment* scheduler_experiment_{nullptr};
};

} // namespace minidb
