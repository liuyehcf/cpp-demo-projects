```sh
cmake -B build -DCMAKE_EXPORT_COMPILE_COMMANDS=ON
cmake --build build
CLASSPATH=build/classes JNI_OPS="-Xmx128M" build/jni_utils_test
```
