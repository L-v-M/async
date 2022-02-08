import subprocess
import os
import itertools

path_to_build_directory = '/home/merzljak/async/build-bench'
path_to_source_directory = '/home/merzljak/async'
path_to_tpch_directory = '/raid0/data/tpch/sf100'
path_to_data_directory = '/raid0/merzljak/data/sf100'
path_to_cxx_compiler = '/usr/bin/g++'
path_to_output = '/home/merzljak/async/benchmark_results/tpch_q1.csv'
numactl = ['numactl', '--membind=0', '--cpubind=0']

page_size_power_list = [16, 17, 18]
num_threads_list = [1, 2, 4, 8, 16, 32, 64, 128]
num_entries_per_ring_list = [2, 4, 8, 16, 32, 64, 128, 256, 512]
num_tuples_per_morsel_list = [500, 1_000, 5_000, 10_000, 100_000]

output = open(path_to_output, 'w')
print_header = "true"

for page_size_power in page_size_power_list:
    # Configure the project
    print(f'Configure the project with page size power of {page_size_power}')
    subprocess.run(['cmake', '-S', path_to_source_directory, '-B', path_to_build_directory, '-DCMAKE_BUILD_TYPE=Release',
                   f'-DASYNCHRONOUS_IO_PAGE_SIZE_POWER={page_size_power}', f'-DCMAKE_CXX_COMPILER={path_to_cxx_compiler}'], check=True, stdout=subprocess.PIPE, stderr=subprocess.PIPE)
    # Build the project
    print(f'Build the project with page size power of {page_size_power}')
    subprocess.run(
        ['cmake', '--build', path_to_build_directory, '--clean-first'], check=True, stdout=subprocess.PIPE, stderr=subprocess.PIPE)
    # Load the data
    print(f'Load the data with page size power of {page_size_power}')
    lineitem_dat = os.path.join(path_to_data_directory, 'lineitem.dat')
    subprocess.run(numactl + [os.path.join(path_to_build_directory, 'storage', 'load_data'),
                   'lineitemQ1', os.path.join(path_to_tpch_directory, 'lineitem.tbl'), lineitem_dat], check=True, stdout=subprocess.PIPE, stderr=subprocess.PIPE)

    # Execute the query using all possible configurations
    for num_threads, num_entries_per_ring, num_tuples_per_morsel in itertools.product(num_threads_list, num_entries_per_ring_list, num_tuples_per_morsel_list):
        print('Run benchmark with the configuration {}'.format({('num_threads', num_threads),
              ('num_entries_per_ring', num_entries_per_ring), ('num_tuples_per_morsel', num_tuples_per_morsel)}))
        subprocess.run(numactl + [os.path.join(path_to_build_directory, 'queries', 'tpch_q1'),
                       lineitem_dat, str(num_threads), str(num_entries_per_ring), str(num_tuples_per_morsel), "true", "false", "false", print_header], check=True, stdout=output, stderr=subprocess.PIPE)
        print_header = "false"
