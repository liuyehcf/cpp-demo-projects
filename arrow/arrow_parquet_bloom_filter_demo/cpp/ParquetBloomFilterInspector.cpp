#include <parquet/file_reader.h>
#include <parquet/metadata.h>
#include <parquet/schema.h>

#include <cstdlib>
#include <iostream>
#include <memory>
#include <stdexcept>
#include <string>
#include <vector>

namespace {

struct Options {
    std::string parquet_file;
    bool show_all = false;
    bool json = false;
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

void PrintText(const std::string& parquet_file, const parquet::FileMetaData& metadata, bool show_all) {
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
                  << " column(s) have bloom filter\n";

        bool printed_any_column = false;
        for (int column_index = 0; column_index < row_group->num_columns(); ++column_index) {
            const std::unique_ptr<parquet::ColumnChunkMetaData> column_chunk = row_group->ColumnChunk(column_index);
            const std::optional<int64_t> offset = column_chunk->bloom_filter_offset();
            const std::optional<int64_t> length = column_chunk->bloom_filter_length();
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
            if (offset.has_value()) {
                std::cout << " (offset=" << *offset;
                if (length.has_value()) {
                    std::cout << ", length=" << *length;
                }
                std::cout << ')';
            }
            std::cout << '\n';
        }

        if (!printed_any_column) {
            std::cout << "  (none)\n";
        }
    }
}

void PrintJson(const std::string& parquet_file, const parquet::FileMetaData& metadata) {
    std::cout << "{\"file\":";
    PrintJsonString(parquet_file);
    std::cout << ",\"row_groups\":[";

    for (int row_group_index = 0; row_group_index < metadata.num_row_groups(); ++row_group_index) {
        if (row_group_index != 0) {
            std::cout << ',';
        }

        const std::unique_ptr<parquet::RowGroupMetaData> row_group = metadata.RowGroup(row_group_index);
        std::cout << "{\"row_group\":" << row_group_index << ",\"columns\":[";

        for (int column_index = 0; column_index < row_group->num_columns(); ++column_index) {
            if (column_index != 0) {
                std::cout << ',';
            }

            const std::unique_ptr<parquet::ColumnChunkMetaData> column_chunk = row_group->ColumnChunk(column_index);
            const std::optional<int64_t> offset = column_chunk->bloom_filter_offset();
            const std::optional<int64_t> length = column_chunk->bloom_filter_length();

            std::cout << "{\"path\":";
            PrintJsonString(column_chunk->path_in_schema()->ToDotString());
            std::cout << ",\"has_bloom_filter\":" << (offset.has_value() ? "true" : "false");
            std::cout << ",\"bloom_filter_offset\":";
            PrintOptionalInt64(offset);
            std::cout << ",\"bloom_filter_length\":";
            PrintOptionalInt64(length);
            std::cout << '}';
        }

        std::cout << "]}";
    }

    std::cout << "]}\n";
}

void Run(int argc, char** argv) {
    const Options options = ParseOptions(argc, argv);
    const std::unique_ptr<parquet::ParquetFileReader> reader =
            parquet::ParquetFileReader::OpenFile(options.parquet_file, false);
    const std::shared_ptr<parquet::FileMetaData> metadata = reader->metadata();

    if (options.json) {
        PrintJson(options.parquet_file, *metadata);
    } else {
        PrintText(options.parquet_file, *metadata, options.show_all);
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
