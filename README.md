# What Are You Waiting For? Use Coroutines for Asynchronous I/O to Hide I/O Latencies and Maximize the Read Bandwidth!

This repository contains micro-benchmarks to examine the benefits of asynchronous I/O for query processing using C++-Coroutines and `io_uring`.

## Build

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

We only support building with GCC 11.3.0 or newer.
You can specify the size of a database page by setting `-DASYNCHRONOUS_IO_PAGE_SIZE_POWER` (default: 16).
An `ASYNCHRONOUS_IO_PAGE_SIZE_POWER` of 16 means 2^16 bytes per page.
`ASYNCHRONOUS_IO_PAGE_SIZE_POWER` must be in the range [12, 22].

```
mkdir build
cd build
cmake -G "Ninja" -DCMAKE_C_COMPILER=$(which gcc) -DCMAKE_CXX_COMPILER=$(which g++) -DCMAKE_BUILD_TYPE=Release -DASYNCHRONOUS_IO_PAGE_SIZE_POWER=19 ..
ninja
```

## Load Data

Before you can load the data into our custom format, you need to generate it.
Note, that you can specify the scale factor after the `-s` (default: 1).
Scale factor of 1 is 1 GB of data.
Adapt the path below to control in which directory the files are created.

```
git clone https://github.com/electrum/tpch-dbgen.git
cd tpch-dbgen
make
DSS_PATH=/PATH/TO/DIR ./dbgen -s 1
```

Here is the help message of the `load_data` executable:

```
./build/storage/load_data --help
Usage: ./build/storage/load_data lineitemQ1 lineitem.tbl lineitemQ1.dat | lineitemQ14 lineitem.tbl lineitemQ14.dat | part part.tbl part.dat
```

To actually load the data, execute the following commands:

```
./build/storage/load_data lineitemQ1 data/lineitem.tbl data/lineitemQ1.dat
./build/storage/load_data lineitemQ14 data/lineitem.tbl data/lineitemQ14.dat
./build/storage/load_data part data/part.tbl data/part.dat
```

## Query 1

### Usage

```
./build/queries/tpch_q1 --help
Usage: ./build/queries/tpch_q1 lineitem.dat num_threads num_entries_per_ring num_tuples_per_morsel do_work do_random_io print_result print_header
```

### Example

```
./build/queries/tpch_q1 data/lineitemQ1.dat 128 128 1000 true true true true
```

## Query 14

### Usage

```
./build/queries/tpch_q14 --help
Usage: ./build/queries/tpch_q14 lineitem.dat part.dat num_threads num_entries_per_ring num_tuples_per_coroutine print_result print_header
```

### Example

```
./build/queries/tpch_q14 data/lineitemQ14.dat data/part.dat 64 32 1000 true true
```
