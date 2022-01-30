add_executable(gbench_latency gbench_latency.cc)
target_link_libraries(gbench_latency gbench uring)

add_executable(dram_bandwidth dram_bandwidth.cc)
target_link_libraries(dram_bandwidth Threads::Threads)