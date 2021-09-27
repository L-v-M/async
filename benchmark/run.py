import subprocess
import os
import itertools

path_to_build_directory = "/home/leonard/Desktop/async/build2"
path_to_source_directory = "/home/leonard/Desktop/async"
path_to_tpch_directory = "/home/leonard/tpch_data/sf5"
path_to_data_directory = "/home/leonard/Desktop/async/data/sf5"
path_to_cxx_compiler = "/usr/bin/g++-11"
path_to_query1_out = "/home/leonard/Desktop/async/query1_out.csv"
path_to_query14_out = "/home/leonard/Desktop/async/query14_out.csv"

page_size_powers = [16, 22]
# page_size_powers = [x for x in range(12, 23)]
# num_threads = [x for x in range(1, 9)]
num_threads = [8]
# num_entries_per_ring = [i ** 2 for i in range(2, 7)]
num_entries_per_ring = [4]
do_work = ['true', 'false']
do_random_io = ['false']
# do_random_io = ['true', 'false']
# num_tuples_per_coroutine = [1, 10, 50, 100, 1000, 10000]
num_tuples_per_coroutine = [10000]

query1_out = open(path_to_query1_out, "w")
query14_out = open(path_to_query14_out, "w")
print_header = "true"

for page_size_power in page_size_powers:
    subprocess.run(["cmake", "-S", path_to_source_directory, "-B", path_to_build_directory, "-DCMAKE_BUILD_TYPE=Release",
                   "-DASYNCHRONOUS_IO_PAGE_SIZE_POWER={}".format(page_size_power), "-DCMAKE_CXX_COMPILER={}".format(path_to_cxx_compiler)], check=True, stdout=subprocess.PIPE, stderr=subprocess.PIPE)
    subprocess.run(
        ["cmake", "--build", path_to_build_directory, "--clean-first"], check=True, stdout=subprocess.PIPE, stderr=subprocess.PIPE)
    lineitemq1 = os.path.join(path_to_data_directory, "lineitemQ1.dat")
    lineitemq14 = os.path.join(path_to_data_directory, "lineitemQ14.dat")
    part = os.path.join(path_to_data_directory, "part.dat")
    subprocess.run([os.path.join(path_to_build_directory, "executables", "load_data"),
                   "lineitemQ1", os.path.join(path_to_tpch_directory, "lineitem.tbl"), lineitemq1], check=True, stdout=subprocess.PIPE, stderr=subprocess.PIPE)
    subprocess.run([os.path.join(path_to_build_directory, "executables", "load_data"),
                   "lineitemQ14", os.path.join(path_to_tpch_directory, "lineitem.tbl"), lineitemq14], check=True, stdout=subprocess.PIPE, stderr=subprocess.PIPE)
    subprocess.run([os.path.join(path_to_build_directory, "executables", "load_data"),
                   "part", os.path.join(path_to_tpch_directory, "part.tbl"), part], check=True, stdout=subprocess.PIPE, stderr=subprocess.PIPE)

    for threads, entries_per_ring, work, random_io in itertools.product(num_threads, num_entries_per_ring, do_work, do_random_io):
        subprocess.run([os.path.join(path_to_build_directory, "executables", "tpch_q1"),
                       lineitemq1, str(threads), str(entries_per_ring), work, random_io, "false", print_header], check=True, stdout=query1_out, stderr=subprocess.PIPE)
        print_header = "false"

    print_header = "true"
    for threads, entries_per_ring, tuples_per_coroutine in itertools.product(num_threads, num_entries_per_ring, num_tuples_per_coroutine):
        subprocess.run([os.path.join(path_to_build_directory, "executables", "tpch_q14"),
                       lineitemq14, part, str(threads), str(entries_per_ring), str(tuples_per_coroutine), "false", print_header], check=True, stdout=query14_out, stderr=subprocess.PIPE)
        print_header = "false"
