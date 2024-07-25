# Build

```sh
cmake -B build
cmake --build build
```

# Test

Use bthread (This will crash):

```sh
CLASSPATH=build/classes build/echo_server 0
build/echo_client
```

Use pthread instead of bthread (This works fine):

```sh
CLASSPATH=build/classes build/echo_server 1
build/echo_client
```

