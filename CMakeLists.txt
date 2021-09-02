cmake_minimum_required(VERSION 3.16)

project(asynchronousIO)

set(CMAKE_CXX_STANDARD 20)
add_compile_options(-Wall -Wextra -Werror -march=native -fcoroutines)

add_subdirectory(cppcoro)
add_subdirectory(executables)
add_subdirectory(storage)