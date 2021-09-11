# Build

We depend on [liburing](https://github.com/axboe/liburing).
```
git clone https://github.com/axboe/liburing.git
cd liburing
./configure
make all
```

Add the following to the CMakeLists.txt:
```
add_library(uring STATIC IMPORTED)
set_target_properties(uring PROPERTIES
  IMPORTED_LOCATION "/home/merzljak/liburing/src/liburing.a"
  INTERFACE_INCLUDE_DIRECTORIES "/home/merzljak/liburing/src/include"
)
```
