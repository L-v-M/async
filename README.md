# What are you waiting for?
## Use coroutines for asynchronous I/O to hide I/O latencies and maximize the read bandwidth!

This repository contains micro-benchmarks to examine the benefits of asynchronous I/O for query processing using C++-Coroutines and `io_uring`.

### Build

We depend on [liburing](https://github.com/axboe/liburing):

```
git clone https://github.com/axboe/liburing.git
cd liburing
./configure
make
```

Now, you can either make `liburing` available system-wide:

```
sudo make install
```

Or, you can uncomment and adapt the following lines in the project's `CMakeLists.txt`:

```
add_library(uring STATIC IMPORTED)
set_target_properties(uring PROPERTIES
  IMPORTED_LOCATION "/PATH_TO_LIBURING/liburing/src/liburing.a"
  INTERFACE_INCLUDE_DIRECTORIES "/PATH_TO_LIBURING/liburing/src/include"
)
```

We only support building with GCC 11.1.0 or newer. You can specify the size of a database page by setting `-DASYNCHRONOUS_IO_PAGE_SIZE_POWER` (default: 16). A `ASYNCHRONOUS_IO_PAGE_SIZE_POWER` of 16 means 2^16 bytes per page.
`ASYNCHRONOUS_IO_PAGE_SIZE_POWER` must be in the range [12, 22].

```
mkdir build
cd build
cmake -G "Ninja" -DCMAKE_C_COMPILER=$(which gcc) -DCMAKE_CXX_COMPILER=$(which g++) -DCMAKE_BUILD_TYPE=Release -DASYNCHRONOUS_IO_PAGE_SIZE_POWER=19 ..
ninja
```

### Load Data

Before you can load the data into our custom format, you need to generate it. Note, that you can specify the scale factor after the `-s` (default: 1). Scale factor of 1 is 1 GB of data.

```
git clone https://github.com/electrum/tpch-dbgen.git
cd tpch-dbgen
make
./dbgen -s 1
```

Here is the help message of the `load_data` executable:

```
./build/Release/storage/load_data --help
Usage: ./build/Release/storage/load_data lineitemQ1 lineitem.tbl lineitemQ1.dat | lineitemQ14 lineitem.tbl lineitemQ14.dat | part part.tbl part.dat
```

To actually load the data, execute the following commands:

```
numactl --membind=0 --cpubind=0 ./build/Release/storage/load_data lineitemQ1 /raid0/data/tpch/sf100/lineitem.tbl /raid0/merzljak/data/sf100/lineitemQ1.dat
numactl --membind=0 --cpubind=0 ./build/Release/storage/load_data lineitemQ14 /raid0/data/tpch/sf100/lineitem.tbl /raid0/merzljak/data/sf100/lineitemQ14.dat
numactl --membind=0 --cpubind=0 ./build/Release/storage/load_data part /raid0/data/tpch/sf100/part.tbl /raid0/merzljak/data/sf100/part.dat
```

## Query 1

### Usage

```
./build/Release/queries/tpch_q1 --help
Usage: ./build/Release/queries/tpch_q1 lineitem.dat num_threads num_entries_per_ring num_tuples_per_morsel do_work do_random_io print_result print_header
```

### Example

```
numactl --membind=0 --cpubind=0 ./build/Release/queries/tpch_q1 /raid0/merzljak/data/sf100/lineitemQ1.dat 128 128 1000 true false false true
```

## Query 14

### Usage

```
./build/Release/queries/tpch_q14 --help
Usage: ./build/Release/queries/tpch_q14 lineitem.dat part.dat num_threads num_entries_per_ring num_tuples_per_coroutine print_result print_header
```

### Example

```
numactl --membind=0 --cpubind=0 ./build/Release/queries/tpch_q14 /raid0/merzljak/data/sf10/lineitemQ14.dat /raid0/merzljak/data/sf10/part.dat 64 32 1000 true true
```
