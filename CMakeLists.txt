cmake_minimum_required(VERSION 3.16)

project(asynchronousIO)

set(CMAKE_CXX_STANDARD 20)
add_compile_options(-Wall -Wextra -Werror -march=native -fcoroutines)

# add_library(uring STATIC IMPORTED)
# set_target_properties(uring PROPERTIES
#   IMPORTED_LOCATION "/PATH_TO_LIBURING/liburing/src/liburing.a"
#   INTERFACE_INCLUDE_DIRECTORIES "/PATH_TO_LIBURING/liburing/src/include"
# )

if(NOT DEFINED ASYNCHRONOUS_IO_PAGE_SIZE_POWER)
    set(ASYNCHRONOUS_IO_PAGE_SIZE_POWER 16)
endif()

add_compile_definitions(ASYNCHRONOUS_IO_PAGE_SIZE_POWER=${ASYNCHRONOUS_IO_PAGE_SIZE_POWER})
message("ASYNCHRONOUS_IO_PAGE_SIZE_POWER = ${ASYNCHRONOUS_IO_PAGE_SIZE_POWER}")

add_subdirectory(cppcoro)
add_subdirectory(executables)
add_subdirectory(storage)