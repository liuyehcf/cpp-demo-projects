#include <arrow/api.h>
#include <arrow/c/bridge.h>
#include <arrow/io/api.h>
#include <arrow/ipc/api.h>

#include <iostream>
#include <memory>
#include <vector>

#include "lance_ffi.h"

#define ASSERT_TRUE(expr, message)                                 \
    if (!(expr)) {                                                 \
        std::cerr << "Assertion failed: " << message << std::endl; \
        return -1;                                                 \
    }

// Helper function to create Arrow data and export to ArrowArrayStream
int create_arrow_stream(const std::vector<int32_t>& ids, const std::vector<std::string>& names,
                        const std::vector<int32_t>& values, struct ArrowArrayStream* out_stream) {
    // Create Arrow arrays
    arrow::Int32Builder id_builder;
    arrow::StringBuilder name_builder;
    arrow::Int32Builder value_builder;

    for (size_t i = 0; i < ids.size(); i++) {
        auto status = id_builder.Append(ids[i]);
        ASSERT_TRUE(status.ok(), "Failed to append id: " + status.ToString());

        status = name_builder.Append(names[i]);
        ASSERT_TRUE(status.ok(), "Failed to append name: " + status.ToString());

        status = value_builder.Append(values[i]);
        ASSERT_TRUE(status.ok(), "Failed to append value: " + status.ToString());
    }

    std::shared_ptr<arrow::Array> id_array;
    std::shared_ptr<arrow::Array> name_array;
    std::shared_ptr<arrow::Array> value_array;

    auto status = id_builder.Finish(&id_array);
    ASSERT_TRUE(status.ok(), "Failed to finish id array: " + status.ToString());

    status = name_builder.Finish(&name_array);
    ASSERT_TRUE(status.ok(), "Failed to finish name array: " + status.ToString());

    status = value_builder.Finish(&value_array);
    ASSERT_TRUE(status.ok(), "Failed to finish value array: " + status.ToString());

    // Create schema
    auto schema = arrow::schema({arrow::field("id", arrow::int32()), arrow::field("name", arrow::utf8()),
                                 arrow::field("value", arrow::int32())});

    // Create record batch
    auto batch = arrow::RecordBatch::Make(schema, ids.size(), {id_array, name_array, value_array});

    // Create a vector containing the single batch
    std::vector<std::shared_ptr<arrow::RecordBatch>> batches = {batch};

    // Create a RecordBatchReader from the batches
    auto reader = arrow::RecordBatchReader::Make(batches, schema).ValueOrDie();

    // Export to ArrowArrayStream using Arrow's C data interface
    auto export_result = arrow::ExportRecordBatchReader(reader, out_stream);
    ASSERT_TRUE(export_result.ok(), "Failed to export RecordBatchReader");

    std::cout << "Created Arrow stream with " << ids.size() << " rows" << std::endl;
    return 0;
}

// Helper function to read ArrowArrayStream and display data
int display_arrow_stream(struct ArrowArrayStream* stream) {
    // Import ArrowArrayStream to Arrow C++ RecordBatchReader
    auto reader_result = arrow::ImportRecordBatchReader(stream);
    ASSERT_TRUE(reader_result.ok(), "Failed to import ArrowArrayStream");
    auto reader = reader_result.ValueOrDie();

    // Read all batches
    while (true) {
        auto batch_result = reader->Next();
        ASSERT_TRUE(batch_result.ok(), "Failed to read next batch");

        auto batch = batch_result.ValueOrDie();
        if (!batch) break; // End of stream

        // Display data
        auto id_array = std::static_pointer_cast<arrow::Int32Array>(batch->column(0));
        auto name_array = std::static_pointer_cast<arrow::StringArray>(batch->column(1));
        auto value_array = std::static_pointer_cast<arrow::Int32Array>(batch->column(2));

        for (int64_t i = 0; i < batch->num_rows(); i++) {
            std::cout << "  ID: " << id_array->Value(i) << ", Name: " << name_array->GetString(i)
                      << ", Value: " << value_array->Value(i) << std::endl;
        }
    }
    return 0;
}

int main() {
    std::cout << "=== Lance C++/Rust FFI Demo with Arrow Streams ===" << std::endl << std::endl;

    // Initialize Lance dataset
    std::cout << "1. Initializing Lance dataset..." << std::endl;
    int result = lance_init("./lance_dataset");
    ASSERT_TRUE(result == 0, "Lance dataset initialization failed");
    std::cout << "Database initialized successfully!" << std::endl << std::endl;

    // Create table
    std::cout << "2. Creating table 'users'..." << std::endl;
    result = lance_create_table("users");
    ASSERT_TRUE(result == 0, "Table creation failed");
    std::cout << "Table 'users' created successfully!" << std::endl << std::endl;

    // Create Arrow data and write to table
    std::cout << "3. Creating Arrow data and writing to table..." << std::endl;
    std::vector<int32_t> ids = {1, 2, 3, 4, 5};
    std::vector<std::string> names = {"Alice", "Bob", "Charlie", "Diana", "Eve"};
    std::vector<int32_t> values = {25, 30, 35, 28, 32};

    struct ArrowArrayStream write_stream;
    result = create_arrow_stream(ids, names, values, &write_stream);
    ASSERT_TRUE(result == 0, "Failed to create Arrow stream");

    result = lance_write_arrow_stream("users", &write_stream);
    ASSERT_TRUE(result == 0, "Failed to write Arrow stream data");
    std::cout << "Arrow stream data written successfully!" << std::endl << std::endl;

    // Read data as Arrow stream
    std::cout << "4. Reading data as Arrow stream..." << std::endl;
    struct ArrowArrayStream read_stream;

    result = lance_read_arrow_stream("users", &read_stream);
    ASSERT_TRUE(result == 0, "Failed to read Arrow stream data");
    std::cout << "Received Arrow stream:" << std::endl;
    display_arrow_stream(&read_stream);
    std::cout << "Arrow stream data read successfully!" << std::endl << std::endl;

    std::cout << "=== Demo completed successfully! ===" << std::endl;
    std::cout << std::endl;
    std::cout << "Note: All data exchange used Arrow IPC streams between C++ and Rust" << std::endl;

    // Cleanup
    lance_cleanup();

    return 0;
}
