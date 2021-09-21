# Asynchronous I/O for Query Processing

Micro-Benchmarks to evaluate the benefits of asynchronous I/O for query processing using C++-Coroutines and `io_uring`.

## Build

We depend on [liburing](https://github.com/axboe/liburing).

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
