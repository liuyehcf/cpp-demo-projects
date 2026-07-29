#include <arrow/api.h>
#include <arrow/io/api.h>
#include <arrow/result.h>
#include <arrow/status.h>
#include <arrow/util/decimal.h>
#include <parquet/arrow/writer.h>
#include <parquet/file_reader.h>
#include <parquet/properties.h>

#include <cstdint>
#include <cstdlib>
#include <iostream>
#include <memory>
#include <stdexcept>
#include <string>

namespace {

constexpr int kRowGroupCount = 4;
constexpr int kRowsPerRowGroup = 3;
constexpr int kBoundaryRowsPerRowGroup = 2;
constexpr int kBaseRowsPerRowGroup = kRowsPerRowGroup - kBoundaryRowsPerRowGroup;
constexpr int kDecimalScale = 2;
constexpr int kDecimalPrecision = 12;

constexpr int32_t kSharedIntLowerBound = -100;
constexpr int32_t kSharedIntUpperBound = 100;
constexpr double kSharedDoubleLowerBound = -100.0;
constexpr double kSharedDoubleUpperBound = 100.0;
constexpr char kSharedDecimalLowerBound[] = "-100.00";
constexpr char kSharedDecimalUpperBound[] = "100.00";
constexpr char kSharedStringLowerBound[] = "aaa_shared_low";
constexpr char kSharedStringUpperBound[] = "zzz_shared_high";

#define THROW_NOT_OK(expr)                                                               \
    do {                                                                                 \
        const ::arrow::Status _status = (expr);                                          \
        if (!_status.ok()) {                                                             \
            throw std::runtime_error(std::string("Arrow error: ") + _status.ToString()); \
        }                                                                                \
    } while (false)

#define ASSIGN_OR_THROW(lhs, rexpr)                                                               \
    do {                                                                                          \
        auto _result = (rexpr);                                                                   \
        if (!_result.ok()) {                                                                      \
            throw std::runtime_error(std::string("Arrow error: ") + _result.status().ToString()); \
        }                                                                                         \
        lhs = std::move(_result).ValueUnsafe();                                                   \
    } while (false)

std::string DecimalTextForRowGroup(int row_group) {
    // With scale=2, row groups 0/1/2/3 map to 10.00/20.00/30.00/40.00 respectively.
    return std::to_string((row_group + 1) * 10) + ".00";
}

arrow::Decimal128 DecimalValueForRowGroup(int row_group) {
    arrow::Decimal128 value;
    int32_t parsed_precision = 0;
    int32_t parsed_scale = 0;
    THROW_NOT_OK(
            arrow::Decimal128::FromString(DecimalTextForRowGroup(row_group), &value, &parsed_precision, &parsed_scale));
    if (parsed_scale != kDecimalScale || parsed_precision > kDecimalPrecision) {
        throw std::runtime_error("unexpected decimal precision/scale");
    }
    return value;
}

arrow::Decimal128 DecimalValueFromText(const std::string& text) {
    arrow::Decimal128 value;
    int32_t parsed_precision = 0;
    int32_t parsed_scale = 0;
    THROW_NOT_OK(arrow::Decimal128::FromString(text, &value, &parsed_precision, &parsed_scale));
    if (parsed_scale != kDecimalScale || parsed_precision > kDecimalPrecision) {
        throw std::runtime_error("unexpected decimal precision/scale");
    }
    return value;
}

std::shared_ptr<arrow::Array> FinishArray(arrow::ArrayBuilder* builder) {
    std::shared_ptr<arrow::Array> array;
    THROW_NOT_OK(builder->Finish(&array));
    return array;
}

std::shared_ptr<arrow::Table> BuildDemoTable() {
    arrow::Int32Builder int_builder;
    arrow::DoubleBuilder double_builder;
    arrow::Decimal128Builder decimal_builder(arrow::decimal128(kDecimalPrecision, kDecimalScale));
    arrow::StringBuilder string_builder;

    if (kBaseRowsPerRowGroup <= 0) {
        throw std::runtime_error("kRowsPerRowGroup must be greater than boundary rows");
    }

    const arrow::Decimal128 shared_decimal_lower_bound = DecimalValueFromText(kSharedDecimalLowerBound);
    const arrow::Decimal128 shared_decimal_upper_bound = DecimalValueFromText(kSharedDecimalUpperBound);

    for (int row_group = 0; row_group < kRowGroupCount; ++row_group) {
        const int32_t int_value = row_group + 1;
        const double double_value = (row_group + 1) * 1.25;
        const arrow::Decimal128 decimal_value = DecimalValueForRowGroup(row_group);
        const std::string string_value = "str_rg_" + std::to_string(row_group + 1);

        // Key point:
        // 1. Each row group still has its own "business" value per column, so equality predicates
        //    such as int_col=1 or string_col='str_rg_3' only exist in one row group.
        // 2. Every row group also gets the same lower/upper boundary value for every column,
        //    making min/max statistics overlap across all row groups. This prevents external
        //    readers from safely skipping row groups via statistics alone, so bloom filters
        //    become the main skipping mechanism for those unique equality predicates.
        for (int row = 0; row < kBaseRowsPerRowGroup; ++row) {
            THROW_NOT_OK(int_builder.Append(int_value));
            THROW_NOT_OK(double_builder.Append(double_value));
            THROW_NOT_OK(decimal_builder.Append(decimal_value));
            THROW_NOT_OK(string_builder.Append(string_value));
        }

        THROW_NOT_OK(int_builder.Append(kSharedIntLowerBound));
        THROW_NOT_OK(double_builder.Append(kSharedDoubleLowerBound));
        THROW_NOT_OK(decimal_builder.Append(shared_decimal_lower_bound));
        THROW_NOT_OK(string_builder.Append(kSharedStringLowerBound));

        THROW_NOT_OK(int_builder.Append(kSharedIntUpperBound));
        THROW_NOT_OK(double_builder.Append(kSharedDoubleUpperBound));
        THROW_NOT_OK(decimal_builder.Append(shared_decimal_upper_bound));
        THROW_NOT_OK(string_builder.Append(kSharedStringUpperBound));
    }

    auto schema = arrow::schema({arrow::field("int_col", arrow::int32()), arrow::field("double_col", arrow::float64()),
                                 arrow::field("decimal_col", arrow::decimal128(kDecimalPrecision, kDecimalScale)),
                                 arrow::field("string_col", arrow::utf8())});
    return arrow::Table::Make(schema, {FinishArray(&int_builder), FinishArray(&double_builder),
                                       FinishArray(&decimal_builder), FinishArray(&string_builder)});
}

parquet::BloomFilterOptions MakeBloomFilterOptions() {
    parquet::BloomFilterOptions options;
    options.ndv = 3;
    options.fpp = 0.001;
    options.fold = false;
    return options;
}

std::string ParseOutputFile(int argc, char** argv) {
    if (argc != 2) {
        throw std::runtime_error("usage: ParquetWithBloomFilterWriter <parquet-file>");
    }
    return argv[1];
}

void PrintNativeBloomFilterSummary(const std::string& output_file) {
    auto parquet_reader = parquet::ParquetFileReader::OpenFile(output_file, false);
    const auto metadata = parquet_reader->metadata();

    std::cout << "Verified native bloom filter metadata in " << output_file << ":\n";
    for (int row_group = 0; row_group < metadata->num_row_groups(); ++row_group) {
        const auto row_group_metadata = metadata->RowGroup(row_group);
        for (int column_index = 0; column_index < metadata->num_columns(); ++column_index) {
            const auto column = metadata->schema()->Column(column_index);
            const auto column_chunk = row_group_metadata->ColumnChunk(column_index);
            std::cout << "  row group " << row_group << ", column " << column->path()->ToDotString() << " -> ";
            if (!column_chunk->bloom_filter_offset().has_value()) {
                std::cout << "native bloom filter is missing\n";
                continue;
            }

            std::cout << "offset=" << *column_chunk->bloom_filter_offset();
            if (column_chunk->bloom_filter_length().has_value()) {
                std::cout << ", length=" << *column_chunk->bloom_filter_length();
            }
            std::cout << '\n';
        }
    }
}

void WriteDemoParquetFile(const std::string& output_file) {
    const auto table = BuildDemoTable();

    std::shared_ptr<arrow::io::FileOutputStream> outfile;
    ASSIGN_OR_THROW(outfile, arrow::io::FileOutputStream::Open(output_file));

    const parquet::BloomFilterOptions bloom_filter_options = MakeBloomFilterOptions();
    parquet::WriterProperties::Builder builder;
    builder.created_by("arrow_parquet_bloom_filter_demo");
    builder.disable_dictionary();
    builder.max_row_group_length(kRowsPerRowGroup);
    builder.enable_bloom_filter("int_col", bloom_filter_options);
    builder.enable_bloom_filter("double_col", bloom_filter_options);
    builder.enable_bloom_filter("decimal_col", bloom_filter_options);
    builder.enable_bloom_filter("string_col", bloom_filter_options);
    auto writer_properties = builder.build();

    parquet::ArrowWriterProperties::Builder arrow_builder;
    auto arrow_writer_properties = arrow_builder.build();

    std::unique_ptr<parquet::arrow::FileWriter> writer;
    ASSIGN_OR_THROW(writer, parquet::arrow::FileWriter::Open(*table->schema(), arrow::default_memory_pool(), outfile,
                                                             writer_properties, arrow_writer_properties));
    THROW_NOT_OK(writer->WriteTable(*table, kRowsPerRowGroup));
    THROW_NOT_OK(writer->Close());

    std::cout << "Wrote " << output_file << " with " << kRowGroupCount
              << " row groups and native Parquet bloom filters for int/double/decimal/string columns.\n";
    PrintNativeBloomFilterSummary(output_file);
}

} // namespace

int main(int argc, char** argv) {
    try {
        WriteDemoParquetFile(ParseOutputFile(argc, argv));
        return EXIT_SUCCESS;
    } catch (const std::exception& ex) {
        std::cerr << "writer failed: " << ex.what() << '\n';
        return EXIT_FAILURE;
    }
}
