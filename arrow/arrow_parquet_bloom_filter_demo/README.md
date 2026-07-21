# Build

```sh
cmake -B build
cmake --build build
build/ParquetWithBloomFilterWriter
build/ParquetWithBloomFilterReader "int_col=1"
build/ParquetWithBloomFilterReader "double_col=3.75"
build/ParquetWithBloomFilterReader "decimal_col=20.00"
build/ParquetWithBloomFilterReader "string_col=str_rg_4"
```
