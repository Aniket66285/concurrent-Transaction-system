cmake_minimum_required(VERSION 3.16)
project(MiniDB_Final LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

find_package(Threads REQUIRED)

file(GLOB_RECURSE MINIDB_CORE_SOURCES CONFIGURE_DEPENDS
    "${PROJECT_SOURCE_DIR}/src/*.cpp"
)

add_library(minidb_core STATIC ${MINIDB_CORE_SOURCES})
target_include_directories(minidb_core PUBLIC "${PROJECT_SOURCE_DIR}/include")
target_link_libraries(minidb_core PUBLIC Threads::Threads)
target_compile_options(minidb_core PRIVATE -Wall -Wextra -Wpedantic)

add_executable(minidb_server apps/server_main.cpp)
target_link_libraries(minidb_server PRIVATE minidb_core Threads::Threads)

# The interactive SQL client speaks HTTP to the running server. It does not
# open the database files itself, so separate terminal clients use independent sessions.
add_executable(minidb_client apps/client_main.cpp)
target_link_libraries(minidb_client PRIVATE Threads::Threads)
target_compile_options(minidb_client PRIVATE -Wall -Wextra -Wpedantic)

enable_testing()
add_executable(scheduler_experiment_test tests/scheduler_experiment_test.cpp)
target_link_libraries(scheduler_experiment_test PRIVATE minidb_core Threads::Threads)
add_test(NAME scheduler_experiment_test COMMAND scheduler_experiment_test)

add_executable(worker_pool_metrics_test tests/worker_pool_metrics_test.cpp)
target_link_libraries(worker_pool_metrics_test PRIVATE minidb_core Threads::Threads)
add_test(NAME worker_pool_metrics_test COMMAND worker_pool_metrics_test)
