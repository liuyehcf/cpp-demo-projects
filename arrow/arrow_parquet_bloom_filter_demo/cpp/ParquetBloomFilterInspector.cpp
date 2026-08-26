#include <arrow/io/api.h>
#include <parquet/file_reader.h>
#include <parquet/metadata.h>
#include <parquet/schema.h>
#include <parquet/thrift_internal.h>

#include <cstdlib>
#include <iostream>
#include <memory>
#include <optional>
#include <stdexcept>
#include <string>
#include <vector>

namespace {

constexpr int64_t kBloomFilterHeaderSizeGuess = 256;

struct Options {
    std::string parquet_file;
    bool show_all = false;
    bool json = false;
};

struct BloomFilterInfo {
    std::optional<int64_t> length;
    std::optional<int32_t> bitset_bytes;
    std::optional<std::string> algorithm;
    std::optional<std::string> hash;
    std::optional<std::string> compression;
};

void PrintUsage(const char* program) {
    std::cerr << "usage: " << program << " <parquet-file> [--all] [--json]\n"
              << "  --all   show columns without bloom filters as well\n"
              << "  --json  print machine-readable JSON\n";
}

Options ParseOptions(int argc, char** argv) {
    if (argc < 2) {
        PrintUsage(argv[0]);
        throw std::runtime_error("missing parquet file");
    }

    Options options;
    for (int i = 1; i < argc; ++i) {
        const std::string arg = argv[i];
        if (arg == "--all") {
            options.show_all = true;
        } else if (arg == "--json") {
            options.json = true;
        } else if (arg == "-h" || arg == "--help") {
            PrintUsage(argv[0]);
            std::exit(EXIT_SUCCESS);
        } else if (!options.parquet_file.empty()) {
            PrintUsage(argv[0]);
            throw std::runtime_error("only one parquet file can be specified");
        } else {
            options.parquet_file = arg;
        }
    }

    if (options.parquet_file.empty()) {
        PrintUsage(argv[0]);
        throw std::runtime_error("missing parquet file");
    }
    return options;
}

void PrintJsonString(const std::string& value) {
    std::cout << '"';
    for (const unsigned char c : value) {
        switch (c) {
        case '"':
            std::cout << "\\\"";
            break;
        case '\\':
            std::cout << "\\\\";
            break;
        case '\b':
            std::cout << "\\b";
            break;
        case '\f':
            std::cout << "\\f";
            break;
        case '\n':
            std::cout << "\\n";
            break;
        case '\r':
            std::cout << "\\r";
            break;
        case '\t':
            std::cout << "\\t";
            break;
        default:
            if (c < 0x20) {
                constexpr char kHex[] = "0123456789abcdef";
                std::cout << "\\u00" << kHex[c >> 4] << kHex[c & 0x0f];
            } else {
                std::cout << c;
            }
        }
    }
    std::cout << '"';
}

void PrintOptionalInt64(const std::optional<int64_t>& value) {
    if (value.has_value()) {
        std::cout << *value;
    } else {
        std::cout << "null";
    }
}

std::optional<int64_t> GetStatsDistinctCount(const parquet::ColumnChunkMetaData& column_chunk) {
    if (!column_chunk.is_stats_set()) {
        return std::nullopt;
    }

    const std::shared_ptr<parquet::Statistics> statistics = column_chunk.statistics();
    if (!statistics || !statistics->HasDistinctCount()) {
        return std::nullopt;
    }
    return statistics->distinct_count();
}

std::string BloomFilterAlgorithmName(const parquet::format::BloomFilterAlgorithm& algorithm) {
    if (algorithm.__isset.BLOCK) {
        return "BLOCK";
    }
    return "UNKNOWN";
}

std::string BloomFilterHashName(const parquet::format::BloomFilterHash& hash) {
    if (hash.__isset.XXHASH) {
        return "XXHASH";
    }
    return "UNKNOWN";
}

std::string BloomFilterCompressionName(const parquet::format::BloomFilterCompression& compression) {
    if (compression.__isset.UNCOMPRESSED) {
        return "UNCOMPRESSED";
    }
    return "UNKNOWN";
}

BloomFilterInfo GetBloomFilterInfo(const std::shared_ptr<arrow::io::RandomAccessFile>& file,
                                   const parquet::ColumnChunkMetaData& column_chunk) {
    BloomFilterInfo info;
    info.length = column_chunk.bloom_filter_length();

    const std::optional<int64_t> offset = column_chunk.bloom_filter_offset();
    if (!offset.has_value() || column_chunk.crypto_metadata() != nullptr) {
        return info;
    }

    try {
        auto header_buffer_result = file->ReadAt(*offset, kBloomFilterHeaderSizeGuess);
        if (!header_buffer_result.ok()) {
            return info;
        }

        std::shared_ptr<arrow::Buffer> header_buffer = std::move(header_buffer_result).ValueUnsafe();
        parquet::format::BloomFilterHeader header;
        parquet::ThriftDeserializer deserializer(parquet::default_reader_properties());
        uint32_t header_size = static_cast<uint32_t>(header_buffer->size());
        deserializer.DeserializeMessage(header_buffer->data(), &header_size, &header);
        if (header.numBytes <= 0) {
            return info;
        }

        info.bitset_bytes = header.numBytes;
        info.algorithm = BloomFilterAlgorithmName(header.algorithm);
        info.hash = BloomFilterHashName(header.hash);
        info.compression = BloomFilterCompressionName(header.compression);
        if (!info.length.has_value()) {
            info.length = static_cast<int64_t>(header_size) + header.numBytes;
        }
    } catch (const std::exception&) {
        return info;
    }
    return info;
}

void PrintText(const std::string& parquet_file, const std::shared_ptr<arrow::io::RandomAccessFile>& file,
               const parquet::FileMetaData& metadata, bool show_all) {
    std::cout << "File: " << parquet_file << '\n';
    std::cout << "Row groups: " << metadata.num_row_groups() << '\n';

    for (int row_group_index = 0; row_group_index < metadata.num_row_groups(); ++row_group_index) {
        const std::unique_ptr<parquet::RowGroupMetaData> row_group = metadata.RowGroup(row_group_index);

        int bloom_filter_count = 0;
        for (int column_index = 0; column_index < row_group->num_columns(); ++column_index) {
            const std::unique_ptr<parquet::ColumnChunkMetaData> column_chunk = row_group->ColumnChunk(column_index);
            if (column_chunk->bloom_filter_offset().has_value()) {
                ++bloom_filter_count;
            }
        }

        std::cout << "row_group " << row_group_index << ": " << bloom_filter_count << '/' << row_group->num_columns()
                  << " column(s) have bloom filter"
                  << " (rows=" << row_group->num_rows() << ", total_byte_size=" << row_group->total_byte_size()
                  << ", total_compressed_size=" << row_group->total_compressed_size() << ")\n";

        bool printed_any_column = false;
        for (int column_index = 0; column_index < row_group->num_columns(); ++column_index) {
            const std::unique_ptr<parquet::ColumnChunkMetaData> column_chunk = row_group->ColumnChunk(column_index);
            const std::optional<int64_t> offset = column_chunk->bloom_filter_offset();
            const BloomFilterInfo bloom_filter_info = GetBloomFilterInfo(file, *column_chunk);
            const std::optional<int64_t> stats_ndv = GetStatsDistinctCount(*column_chunk);
            if (!show_all && !offset.has_value()) {
                continue;
            }

            printed_any_column = true;
            if (show_all) {
                std::cout << "  [" << (offset.has_value() ? "YES" : "NO ") << "] ";
            } else {
                std::cout << "  - ";
            }

            std::cout << column_chunk->path_in_schema()->ToDotString();
            std::cout << " (values=" << column_chunk->num_values()
                      << ", compressed_size=" << column_chunk->total_compressed_size()
                      << ", uncompressed_size=" << column_chunk->total_uncompressed_size() << ", stats_ndv=";
            PrintOptionalInt64(stats_ndv);
            if (offset.has_value()) {
                std::cout << ", bloom_offset=" << *offset << ", bloom_length=";
                PrintOptionalInt64(bloom_filter_info.length);
                std::cout << ", bloom_bitset_bytes=";
                PrintOptionalInt64(bloom_filter_info.bitset_bytes);
                std::cout << ", bloom_algorithm="
                          << (bloom_filter_info.algorithm.has_value() ? *bloom_filter_info.algorithm : "UNKNOWN")
                          << ", bloom_hash="
                          << (bloom_filter_info.hash.has_value() ? *bloom_filter_info.hash : "UNKNOWN")
                          << ", bloom_compression="
                          << (bloom_filter_info.compression.has_value() ? *bloom_filter_info.compression : "UNKNOWN");
            }
            std::cout << ')';
            std::cout << '\n';
        }

        if (!printed_any_column) {
            std::cout << "  (none)\n";
        }
    }
}

void PrintJson(const std::string& parquet_file, const std::shared_ptr<arrow::io::RandomAccessFile>& file,
               const parquet::FileMetaData& metadata) {
    std::cout << "{\"file\":";
    PrintJsonString(parquet_file);
    std::cout << ",\"row_groups\":[";

    for (int row_group_index = 0; row_group_index < metadata.num_row_groups(); ++row_group_index) {
        if (row_group_index != 0) {
            std::cout << ',';
        }

        const std::unique_ptr<parquet::RowGroupMetaData> row_group = metadata.RowGroup(row_group_index);
        std::cout << "{\"row_group\":" << row_group_index << ",\"num_rows\":" << row_group->num_rows()
                  << ",\"total_byte_size\":" << row_group->total_byte_size()
                  << ",\"total_compressed_size\":" << row_group->total_compressed_size() << ",\"columns\":[";

        for (int column_index = 0; column_index < row_group->num_columns(); ++column_index) {
            if (column_index != 0) {
                std::cout << ',';
            }

            const std::unique_ptr<parquet::ColumnChunkMetaData> column_chunk = row_group->ColumnChunk(column_index);
            const std::optional<int64_t> offset = column_chunk->bloom_filter_offset();
            const BloomFilterInfo bloom_filter_info = GetBloomFilterInfo(file, *column_chunk);
            const std::optional<int64_t> stats_ndv = GetStatsDistinctCount(*column_chunk);

            std::cout << "{\"path\":";
            PrintJsonString(column_chunk->path_in_schema()->ToDotString());
            std::cout << ",\"num_values\":" << column_chunk->num_values();
            std::cout << ",\"total_compressed_size\":" << column_chunk->total_compressed_size();
            std::cout << ",\"total_uncompressed_size\":" << column_chunk->total_uncompressed_size();
            std::cout << ",\"stats_ndv\":";
            PrintOptionalInt64(stats_ndv);
            std::cout << ",\"has_bloom_filter\":" << (offset.has_value() ? "true" : "false");
            std::cout << ",\"bloom_filter_offset\":";
            PrintOptionalInt64(offset);
            std::cout << ",\"bloom_filter_length\":";
            PrintOptionalInt64(bloom_filter_info.length);
            std::cout << ",\"bloom_filter_bitset_bytes\":";
            PrintOptionalInt64(bloom_filter_info.bitset_bytes);
            std::cout << ",\"bloom_filter_algorithm\":";
            if (bloom_filter_info.algorithm.has_value()) {
                PrintJsonString(*bloom_filter_info.algorithm);
            } else {
                std::cout << "null";
            }
            std::cout << ",\"bloom_filter_hash\":";
            if (bloom_filter_info.hash.has_value()) {
                PrintJsonString(*bloom_filter_info.hash);
            } else {
                std::cout << "null";
            }
            std::cout << ",\"bloom_filter_compression\":";
            if (bloom_filter_info.compression.has_value()) {
                PrintJsonString(*bloom_filter_info.compression);
            } else {
                std::cout << "null";
            }
            std::cout << '}';
        }

        std::cout << "]}";
    }

    std::cout << "]}\n";
}

void Run(int argc, char** argv) {
    const Options options = ParseOptions(argc, argv);
    auto file_result = arrow::io::ReadableFile::Open(options.parquet_file);
    if (!file_result.ok()) {
        throw std::runtime_error(std::string("Arrow error: ") + file_result.status().ToString());
    }
    std::shared_ptr<arrow::io::RandomAccessFile> file = std::move(file_result).ValueUnsafe();
    const std::unique_ptr<parquet::ParquetFileReader> reader = parquet::ParquetFileReader::Open(file);
    const std::shared_ptr<parquet::FileMetaData> metadata = reader->metadata();

    if (options.json) {
        PrintJson(options.parquet_file, file, *metadata);
    } else {
        PrintText(options.parquet_file, file, *metadata, options.show_all);
    }
}

} // namespace

int main(int argc, char** argv) {
    try {
        Run(argc, argv);
        return EXIT_SUCCESS;
    } catch (const std::exception& ex) {
        std::cerr << "inspector failed: " << ex.what() << '\n';
        return EXIT_FAILURE;
    }
}
