cmake_minimum_required(VERSION 3.16)

project(async)

set(CMAKE_CXX_STANDARD 20)
add_compile_options(-Wall -Wextra -Werror -march=native -fcoroutines)

add_library(uring STATIC IMPORTED)
set_target_properties(uring PROPERTIES
  IMPORTED_LOCATION "/home/merzljak/liburing/src/liburing.a"
  INTERFACE_INCLUDE_DIRECTORIES "/home/merzljak/liburing/src/include"
)

add_library(gbench STATIC IMPORTED)
set_target_properties(gbench PROPERTIES
  IMPORTED_LOCATION "/home/merzljak/benchmark/build/src/libbenchmark.a"
  INTERFACE_INCLUDE_DIRECTORIES "/home/merzljak/benchmark/include"
)

if(NOT DEFINED ASYNCHRONOUS_IO_PAGE_SIZE_POWER)
    set(ASYNCHRONOUS_IO_PAGE_SIZE_POWER 16)
endif()

add_compile_definitions(ASYNCHRONOUS_IO_PAGE_SIZE_POWER=${ASYNCHRONOUS_IO_PAGE_SIZE_POWER})
message("ASYNCHRONOUS_IO_PAGE_SIZE_POWER = ${ASYNCHRONOUS_IO_PAGE_SIZE_POWER}")

find_package(Threads REQUIRED)

add_subdirectory(bandwidth_and_latency)
add_subdirectory(cppcoro)
add_subdirectory(queries)
add_subdirectory(storage)