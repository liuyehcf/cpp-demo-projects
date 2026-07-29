#include <arrow/api.h>
#include <arrow/status.h>
#include <arrow/util/bit_util.h>
#include <arrow/util/decimal.h>
#include <parquet/arrow/reader.h>
#include <parquet/bloom_filter.h>
#include <parquet/bloom_filter_reader.h>
#include <parquet/file_reader.h>
#include <parquet/schema.h>

#include <array>
#include <cstdint>
#include <cstdlib>
#include <cstring>
#include <iostream>
#include <memory>
#include <stdexcept>
#include <string>
#include <vector>

namespace {

constexpr int kDecimalScale = 2;

#define THROW_NOT_OK(expr)                                                               \
    do {                                                                                 \
        const ::arrow::Status _status = (expr);                                          \
        if (!_status.ok()) {                                                             \
            throw std::runtime_error(std::string("Arrow error: ") + _status.ToString()); \
        }                                                                                \
    } while (false)

struct Predicate {
    std::string column;
    std::string literal;
};

struct Options {
    std::string parquet_file;
    Predicate predicate;
};

Options ParseOptions(int argc, char** argv) {
    if (argc != 3) {
        throw std::runtime_error(
                "usage: ParquetWithBloomFilterReader <parquet-file> <column=value>, e.g. demo.parquet int_col=1");
    }
    const std::string arg = argv[2];
    const size_t pos = arg.find('=');
    if (pos == std::string::npos || pos == 0 || pos + 1 >= arg.size()) {
        throw std::runtime_error("predicate must be in the form column=value");
    }
    return {argv[1], {arg.substr(0, pos), arg.substr(pos + 1)}};
}

bool BloomMayContain(const parquet::BloomFilter& bloom_filter, const parquet::ColumnDescriptor& column,
                     const Predicate& predicate) {
    if (predicate.column == "int_col") {
        const int32_t value = std::stoi(predicate.literal);
        return bloom_filter.FindHash(bloom_filter.Hash(value));
    }
    if (predicate.column == "double_col") {
        const double value = std::stod(predicate.literal);
        return bloom_filter.FindHash(bloom_filter.Hash(value));
    }
    if (predicate.column == "decimal_col") {
        arrow::Decimal128 decimal_value;
        int32_t precision = 0;
        int32_t scale = 0;
        THROW_NOT_OK(arrow::Decimal128::FromString(predicate.literal, &decimal_value, &precision, &scale));
        if (scale != kDecimalScale) {
            throw std::runtime_error("decimal_col predicate scale must be 2, e.g. decimal_col=10.00");
        }

        const int type_length = column.type_length();
        if (type_length <= 0 || type_length > arrow::Decimal128::kByteWidth) {
            throw std::runtime_error("unexpected parquet decimal byte width for bloom filter hashing");
        }

        std::array<uint8_t, arrow::Decimal128::kByteWidth> big_endian_bytes{};
        uint64_t low_bits = 0;
        uint64_t high_bits = 0;
        std::memcpy(&low_bits, decimal_value.native_endian_bytes(), sizeof(low_bits));
        std::memcpy(&high_bits, decimal_value.native_endian_bytes() + sizeof(low_bits), sizeof(high_bits));

        const uint64_t high_bits_be = arrow::bit_util::ToBigEndian(high_bits);
        const uint64_t low_bits_be = arrow::bit_util::ToBigEndian(low_bits);
        std::memcpy(big_endian_bytes.data(), &high_bits_be, sizeof(high_bits_be));
        std::memcpy(big_endian_bytes.data() + sizeof(high_bits_be), &low_bits_be, sizeof(low_bits_be));

        const int offset = arrow::Decimal128::kByteWidth - type_length;
        const parquet::FixedLenByteArray bytes{big_endian_bytes.data() + offset};
        return bloom_filter.FindHash(bloom_filter.Hash(&bytes, type_length));
    }
    if (predicate.column == "string_col") {
        const parquet::ByteArray bytes{static_cast<uint32_t>(predicate.literal.size()),
                                       reinterpret_cast<const uint8_t*>(predicate.literal.data())};
        return bloom_filter.FindHash(bloom_filter.Hash(&bytes));
    }

    throw std::runtime_error("unsupported column in predicate: " + predicate.column);
}

int ResolveColumnIndex(const parquet::FileMetaData& metadata, const std::string& column_name) {
    const parquet::SchemaDescriptor* schema = metadata.schema();
    for (int column_index = 0; column_index < schema->num_columns(); ++column_index) {
        const parquet::ColumnDescriptor* column = schema->Column(column_index);
        if (column->path()->ToDotString() == column_name) {
            return column_index;
        }
    }
    throw std::runtime_error("column not found in parquet schema: " + column_name);
}

std::vector<int> FilterRowGroups(parquet::ParquetFileReader* parquet_reader, const Predicate& predicate) {
    const auto metadata = parquet_reader->metadata();
    const int column_index = ResolveColumnIndex(*metadata, predicate.column);
    const parquet::ColumnDescriptor* column = metadata->schema()->Column(column_index);
    parquet::BloomFilterReader& bloom_filter_reader = parquet_reader->GetBloomFilterReader();

    std::vector<int> selected;
    for (int row_group = 0; row_group < metadata->num_row_groups(); ++row_group) {
        auto row_group_metadata = metadata->RowGroup(row_group);
        auto column_chunk_metadata = row_group_metadata->ColumnChunk(column_index);

        if (!column_chunk_metadata->bloom_filter_offset().has_value()) {
            std::cout << "row group " << row_group << " -> native bloom filter is unavailable, read conservatively"
                      << '\n';
            selected.push_back(row_group);
            continue;
        }

        auto row_group_bloom_filter_reader = bloom_filter_reader.RowGroup(row_group);
        if (!row_group_bloom_filter_reader) {
            std::cout << "row group " << row_group
                      << " -> native bloom filter reader is unavailable, read conservatively" << '\n';
            selected.push_back(row_group);
            continue;
        }

        std::unique_ptr<parquet::BloomFilter> bloom_filter =
                row_group_bloom_filter_reader->GetColumnBloomFilter(column_index);
        if (!bloom_filter) {
            std::cout << "row group " << row_group << " -> native bloom filter is missing, read conservatively" << '\n';
            selected.push_back(row_group);
            continue;
        }

        const bool may_contain = BloomMayContain(*bloom_filter, *column, predicate);
        std::cout << "row group " << row_group << " -> bloom filter says " << (may_contain ? "MAY_MATCH" : "NO_MATCH")
                  << '\n';
        if (may_contain) {
            selected.push_back(row_group);
        }
    }
    return selected;
}

void ReadSelectedRowGroups(const std::string& input_file, const std::vector<int>& row_groups) {
    parquet::arrow::FileReaderBuilder builder;
    THROW_NOT_OK(builder.OpenFile(input_file));
    std::unique_ptr<parquet::arrow::FileReader> reader;
    THROW_NOT_OK(builder.Build(&reader));

    std::shared_ptr<arrow::Table> table;
    THROW_NOT_OK(reader->ReadRowGroups(row_groups, &table));
    std::cout << "\nSelected row groups count=" << row_groups.size() << ", rows=" << table->num_rows() << "\n";
    std::cout << table->ToString() << '\n';
}

void Run(int argc, char** argv) {
    const Options options = ParseOptions(argc, argv);
    const Predicate& predicate = options.predicate;

    auto parquet_reader = parquet::ParquetFileReader::OpenFile(options.parquet_file, false);
    const std::vector<int> row_groups = FilterRowGroups(parquet_reader.get(), predicate);

    if (row_groups.empty()) {
        std::cout << "No row groups matched bloom filter for predicate " << predicate.column << '=' << predicate.literal
                  << '\n';
        return;
    }

    ReadSelectedRowGroups(options.parquet_file, row_groups);
}

} // namespace

int main(int argc, char** argv) {
    try {
        Run(argc, argv);
        return EXIT_SUCCESS;
    } catch (const std::exception& ex) {
        std::cerr << "reader failed: " << ex.what() << '\n';
        return EXIT_FAILURE;
    }
}
