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

constexpr char kOutputFile[] = "parquet_with_bloom_filter.parquet";
constexpr int kRowGroupCount = 4;
constexpr int kRowsPerRowGroup = 8;
constexpr int kDecimalScale = 2;
constexpr int kDecimalPrecision = 12;

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

    for (int row_group = 0; row_group < kRowGroupCount; ++row_group) {
        const int32_t int_value = row_group + 1;
        const double double_value = (row_group + 1) * 1.25;
        const arrow::Decimal128 decimal_value = DecimalValueForRowGroup(row_group);
        const std::string string_value = "str_rg_" + std::to_string(row_group + 1);

        // Key point: for each column, a predicate value appears in only one row group.
        // For example, int_col=1 appears only in row group 0, and double_col=2.5
        // appears only in row group 1.
        for (int row = 0; row < kRowsPerRowGroup; ++row) {
            THROW_NOT_OK(int_builder.Append(int_value));
            THROW_NOT_OK(double_builder.Append(double_value));
            THROW_NOT_OK(decimal_builder.Append(decimal_value));
            THROW_NOT_OK(string_builder.Append(string_value));
        }
    }

    auto schema = arrow::schema({arrow::field("int_col", arrow::int32()), arrow::field("double_col", arrow::float64()),
                                 arrow::field("decimal_col", arrow::decimal128(kDecimalPrecision, kDecimalScale)),
                                 arrow::field("string_col", arrow::utf8())});
    return arrow::Table::Make(schema, {FinishArray(&int_builder), FinishArray(&double_builder),
                                       FinishArray(&decimal_builder), FinishArray(&string_builder)});
}

parquet::BloomFilterOptions MakeBloomFilterOptions() {
    parquet::BloomFilterOptions options;
    options.ndv = kRowsPerRowGroup;
    options.fpp = 0.001;
    options.fold = false;
    return options;
}

void PrintNativeBloomFilterSummary() {
    auto parquet_reader = parquet::ParquetFileReader::OpenFile(kOutputFile, false);
    const auto metadata = parquet_reader->metadata();

    std::cout << "Verified native bloom filter metadata in " << kOutputFile << ":\n";
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

void WriteDemoParquetFile() {
    const auto table = BuildDemoTable();

    std::shared_ptr<arrow::io::FileOutputStream> outfile;
    ASSIGN_OR_THROW(outfile, arrow::io::FileOutputStream::Open(kOutputFile));

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

    std::cout << "Wrote " << kOutputFile << " with " << kRowGroupCount
              << " row groups and native Parquet bloom filters for int/double/decimal/string columns.\n";
    PrintNativeBloomFilterSummary();
}

} // namespace

int main() {
    try {
        WriteDemoParquetFile();
        return EXIT_SUCCESS;
    } catch (const std::exception& ex) {
        std::cerr << "writer failed: " << ex.what() << '\n';
        return EXIT_FAILURE;
    }
}
