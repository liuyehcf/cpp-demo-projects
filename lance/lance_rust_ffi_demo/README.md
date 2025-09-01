# Build

```sh
rm -rf build
cmake -B build -DCMAKE_EXPORT_COMPILE_COMMANDS=ON -DENABLE_ASAN=ON
cmake --build build
```
