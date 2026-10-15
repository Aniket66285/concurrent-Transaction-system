cmake_minimum_required(VERSION 3.16)
project(MiniDB LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
add_compile_options(-Wall -Wextra)

option(MINIDB_SANITIZE "none | address | thread" "none")
if(MINIDB_SANITIZE STREQUAL "address")
  add_compile_options(-fsanitize=address -fno-omit-frame-pointer)
  add_link_options(-fsanitize=address)
elseif(MINIDB_SANITIZE STREQUAL "thread")
  add_compile_options(-fsanitize=thread)
  add_link_options(-fsanitize=thread)
endif()

find_package(Threads REQUIRED)
find_package(GTest REQUIRED)

add_library(minidb
  src/storage/disk_manager.cpp
)
target_include_directories(minidb PUBLIC include)
target_link_libraries(minidb PUBLIC Threads::Threads)

enable_testing()
add_executable(disk_manager_test tests/disk_manager_test.cpp)
target_link_libraries(disk_manager_test PRIVATE minidb GTest::gtest_main)
add_test(NAME disk_manager_test COMMAND disk_manager_test)
