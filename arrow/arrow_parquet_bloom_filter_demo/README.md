# Build

```sh
cmake -B build
cmake --build build
build/ParquetWithBloomFilterWriter build/bloom_filter.parquet

build/ParquetWithBloomFilterReader build/bloom_filter.parquet "int_col=1"
build/ParquetWithBloomFilterReader build/bloom_filter.parquet "double_col=3.75"
build/ParquetWithBloomFilterReader build/bloom_filter.parquet "decimal_col=20.00"
build/ParquetWithBloomFilterReader build/bloom_filter.parquet "string_col=str_rg_4"

build/ParquetBloomFilterInspector build/bloom_filter.parquet
build/ParquetBloomFilterInspector build/bloom_filter.parquet --all
```
