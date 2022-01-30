# Asynchronous I/O for Query Processing

Micro-Benchmarks to examine the benefits of asynchronous I/O for query processing using C++-Coroutines and `io_uring`.

## Build

We depend on [liburing](https://github.com/axboe/liburing):

```
git clone https://github.com/axboe/liburing.git
cd liburing
./configure
make all
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

## Load Data

Before you can load the data into our custom format, you need to generate it. Note, that you can specify the scale factor after the `-s` (default: 1). Scale factor of 1 is 1 GB of data.

```
git clone https://github.com/electrum/tpch-dbgen.git
cd tpch-dbgen.git
make
./dbgen -s 1
```

To actually load the data, execute the following commands. Be prepared to be amazed how fast loading is:

```
./build/executables/load_data lineitemQ1 /raid0/data/tpch/sf100/lineitem.tbl /raid0/merzljak/data/sf100/lineitemQ1.dat
./build/executables/load_data lineitemQ14 /nvmeSpace/merzljak/sf1/lineitem.tbl /nvmeSpace/merzljak/sf1/lineitemQ14.dat
./build/executables/load_data part /nvmeSpace/merzljak/sf1/part.tbl /nvmeSpace/merzljak/sf1/part.dat
```

```
./build/executables/load_data --help
Usage: ./build/executables/load_data lineitemQ1|lineitemQ14|part (lineitem.tbl lineitemQ1.dat)|(lineitem.tbl lineitemQ14.dat)|(part.tbl part.dat)
```

## Query 1

### Usage

```
build/executables/tpch_q1 --help
Usage: build/executables/tpch_q1 lineitem.dat num_threads num_entries_per_ring do_work do_random_io print_result print_header
```

### Example

```
build/executables/tpch_q1 /nvmeSpace/merzljak/sf1/lineitemQ1.dat 20 8 true false true true

numactl --membind=0 --cpubind=0 /home/merzljak/async/build/Release/executables/tpch_q1 /raid0/merzljak/data/sf100/lineitemQ1.dat 32 32 1000 false false false true
```

This will use 20 threads for executing the benchmark. When it uses asynchronous I/O, it will use 8 coroutines per thread. It will actually perform the work of query 1 (instead of only reading the pages and not doing anything with them). And it will output result and header.

## Query 14

TODO