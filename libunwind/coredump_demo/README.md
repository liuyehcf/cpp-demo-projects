# Build

```sh
cmake -B build
cmake --build build -j $(( (cores=$(nproc))>1?cores/2:1 ))
HANDLER_MODE=legacy UNW_MODE=unw_init_local build/coredump_demo
HANDLER_MODE=normal UNW_MODE=unw_init_local build/coredump_demo
HANDLER_MODE=altstack UNW_MODE=unw_init_local build/coredump_demo

HANDLER_MODE=legacy UNW_MODE=unw_init_local2 build/coredump_demo
HANDLER_MODE=normal UNW_MODE=unw_init_local2 build/coredump_demo
HANDLER_MODE=altstack UNW_MODE=unw_init_local2 build/coredump_demo

HANDLER_MODE=legacy UNW_MODE=unw_backtrace build/coredump_demo
HANDLER_MODE=normal UNW_MODE=unw_backtrace build/coredump_demo
HANDLER_MODE=altstack UNW_MODE=unw_backtrace build/coredump_demo
```
