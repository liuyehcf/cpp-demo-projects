# Build

```sh
cmake -B build
cmake --build build -j $(( (cores=$(nproc))>1?cores/2:1 ))
build/coredump_demo
UNW_MODE=unw_init_local build/coredump_demo
UNW_MODE=unw_init_local2 build/coredump_demo
UNW_MODE=unw_backtrace build/coredump_demo
```
