add_executable(tpch_q1 tpch_q1.cc)
target_link_libraries(tpch_q1 Threads::Threads storage)

add_executable(tpch_q14 tpch_q14.cc)
target_link_libraries(tpch_q14 Threads::Threads storage)