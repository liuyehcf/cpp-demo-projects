# Build

```sh
cmake -B build
cmake --build build -j $(( (cores=$(nproc))>1?cores/2:1 ))
build/poco_xml_demo
```
