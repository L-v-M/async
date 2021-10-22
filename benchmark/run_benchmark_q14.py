import subprocess
import os
import itertools

path_to_build_directory = "/home/merzljak/async/build2"
path_to_source_directory = "/home/merzljak/async"
path_to_tpch_directory = "/nvmeSpace/merzljak/sf10"
path_to_data_directory = "/nvmeSpace/merzljak/data/sf10"
path_to_cxx_compiler = "/usr/bin/g++-11"
path_to_query14_out = "/home/merzljak/async/benchmark/query14_out.csv"

page_size_powers_q14 = [12, 14, 16]
num_threads = [1, 5, 10, 15, 20]
num_entries_per_ring = [2, 4, 8, 16, 32, 64]
num_tuples_per_coroutine = [1, 10, 100, 1000]

query14_out = open(path_to_query14_out, "w")
print_header = "true"

for page_size_power in page_size_powers_q14:
    subprocess.run(["cmake", "-S", path_to_source_directory, "-B", path_to_build_directory, "-DCMAKE_BUILD_TYPE=Release",
                   "-DASYNCHRONOUS_IO_PAGE_SIZE_POWER={}".format(page_size_power), "-DCMAKE_CXX_COMPILER={}".format(path_to_cxx_compiler)], check=True, stdout=subprocess.PIPE, stderr=subprocess.PIPE)
    subprocess.run(
        ["cmake", "--build", path_to_build_directory, "--clean-first"], check=True, stdout=subprocess.PIPE, stderr=subprocess.PIPE)
    lineitemq14 = os.path.join(path_to_data_directory, "lineitemQ14.dat")
    part = os.path.join(path_to_data_directory, "part.dat")
    subprocess.run([os.path.join(path_to_build_directory, "executables", "load_data"),
                   "lineitemQ14", os.path.join(path_to_tpch_directory, "lineitem.tbl"), lineitemq14], check=True, stdout=subprocess.PIPE, stderr=subprocess.PIPE)
    subprocess.run([os.path.join(path_to_build_directory, "executables", "load_data"),
                   "part", os.path.join(path_to_tpch_directory, "part.tbl"), part], check=True, stdout=subprocess.PIPE, stderr=subprocess.PIPE)

    print_header = "true"
    for threads, entries_per_ring, tuples_per_coroutine in itertools.product(num_threads, num_entries_per_ring, num_tuples_per_coroutine):
        subprocess.run([os.path.join(path_to_build_directory, "executables", "tpch_q14"),
                       lineitemq14, part, str(threads), str(entries_per_ring), str(tuples_per_coroutine), "false", print_header], check=True, stdout=query14_out, stderr=subprocess.PIPE)
        print_header = "false"
