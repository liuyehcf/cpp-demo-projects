#include <arrow/api.h>
#include <arrow/c/bridge.h>
#include <arrow/io/api.h>
#include <arrow/ipc/api.h>

#include <chrono>
#include <filesystem>
#include <iostream>
#include <memory>
#include <thread>
#include <vector>

#include "lance_ffi.h"

#define ASSERT_TRUE(expr, message)                                 \
    if (!(expr)) {                                                 \
        std::cerr << "Assertion failed: " << message << std::endl; \
        return -1;                                                 \
    }

class TimedRecordBatchReader : public arrow::RecordBatchReader {
public:
    TimedRecordBatchReader(const std::shared_ptr<arrow::Schema>& schema, const std::vector<int32_t>& ids,
                           const std::vector<std::string>& names, const std::vector<int32_t>& values)
            : _schema(schema), _ids(ids), _names(names), _values(values) {}

    std::shared_ptr<arrow::Schema> schema() const override { return _schema; }

    arrow::Status ReadNext(std::shared_ptr<arrow::RecordBatch>* out) override {
        std::cout << "[cpp]:     ReadNext start" << std::endl;
        if (_current_batch >= _ids.size()) {
            *out = nullptr;
            return arrow::Status::OK();
        }

        if (_current_batch > 0) {
            std::cout << "[cpp]:     Waiting 1 seconds before generating next row..." << std::endl;
            std::this_thread::sleep_for(std::chrono::seconds(1));
        }

        // Create Arrow arrays for this batch
        arrow::Int32Builder id_builder;
        arrow::StringBuilder name_builder;
        arrow::Int32Builder value_builder;

        ARROW_RETURN_NOT_OK(id_builder.Append(_ids[_current_batch]));
        ARROW_RETURN_NOT_OK(name_builder.Append(_names[_current_batch]));
        ARROW_RETURN_NOT_OK(value_builder.Append(_values[_current_batch]));

        std::shared_ptr<arrow::Array> id_array;
        std::shared_ptr<arrow::Array> name_array;
        std::shared_ptr<arrow::Array> value_array;

        ARROW_RETURN_NOT_OK(id_builder.Finish(&id_array));
        ARROW_RETURN_NOT_OK(name_builder.Finish(&name_array));
        ARROW_RETURN_NOT_OK(value_builder.Finish(&value_array));

        // Create record batch
        *out = arrow::RecordBatch::Make(_schema, 1, {id_array, name_array, value_array});
        _current_batch++;

        return arrow::Status::OK();
    }

private:
    std::shared_ptr<arrow::Schema> _schema;
    const std::vector<int32_t>& _ids;
    const std::vector<std::string>& _names;
    const std::vector<int32_t>& _values;
    size_t _current_batch = 0;
};

int create_batch_arrow_stream(const std::shared_ptr<arrow::Schema>& schema, const std::vector<int32_t>& ids,
                              const std::vector<std::string>& names, const std::vector<int32_t>& values,
                              struct ArrowArrayStream* out_stream) {
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

    // Create record batch
    auto batch = arrow::RecordBatch::Make(schema, ids.size(), {id_array, name_array, value_array});

    // Create a vector containing the single batch
    std::vector<std::shared_ptr<arrow::RecordBatch>> batches = {batch};

    // Create a RecordBatchReader from the batches
    auto reader = arrow::RecordBatchReader::Make(batches, schema).ValueOrDie();

    // Export to ArrowArrayStream using Arrow's C data interface
    auto export_result = arrow::ExportRecordBatchReader(reader, out_stream);
    ASSERT_TRUE(export_result.ok(), "Failed to export RecordBatchReader");

    std::cout << "[cpp]:     Created batch Arrow stream with " << ids.size() << " rows" << std::endl;
    return 0;
}

int create_customized_arrow_stream(const std::shared_ptr<arrow::Schema>& schema, const std::vector<int32_t>& ids,
                                   const std::vector<std::string>& names, const std::vector<int32_t>& values,
                                   struct ArrowArrayStream* out_stream) {
    auto reader = std::make_shared<TimedRecordBatchReader>(schema, ids, names, values);
    auto export_result = arrow::ExportRecordBatchReader(reader, out_stream);
    ASSERT_TRUE(export_result.ok(), "Failed to export RecordBatchReader");
    std::cout << "[cpp]:     Created customized Arrow stream with " << ids.size() << " rows" << std::endl;

    return 0;
}

int display_arrow_stream(struct ArrowArrayStream* stream) {
    // Import ArrowArrayStream to Arrow C++ RecordBatchReader
    auto reader_result = arrow::ImportRecordBatchReader(stream);
    ASSERT_TRUE(reader_result.ok(), "Failed to import ArrowArrayStream");
    auto reader = reader_result.ValueOrDie();

    std::cout << "[cpp]:     Received Arrow stream:" << std::endl;
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
            std::cout << "[cpp]:         ID: " << id_array->Value(i) << ", Name: " << name_array->GetString(i)
                      << ", Value: " << value_array->Value(i) << std::endl;
        }
    }
    return 0;
}

int main() {
    // Cleanup any existing dataset
    char read_link_res[1024];
    const std::filesystem::path self = (std::string(read_link_res, readlink("/proc/self/exe", read_link_res, 1024)));
    auto dir_path = self.parent_path();
    std::string dataset_path = (dir_path / "lance_dataset").string();
    std::filesystem::remove_all(dataset_path);

    auto schema = arrow::schema({arrow::field("id", arrow::int32()), arrow::field("name", arrow::utf8()),
                                 arrow::field("value", arrow::int32())});

    std::cout << "[cpp]: << Initializing Lance dataset..." << std::endl;
    int result = lance_init(dataset_path.c_str());
    ASSERT_TRUE(result == 0, "Lance dataset initialization failed");

    std::cout << "[cpp]: << Creating table 'users'..." << std::endl;
    result = lance_create_table("users");
    ASSERT_TRUE(result == 0, "Table creation failed");

    std::vector<int32_t> ids_1 = {1, 2, 3, 4, 5};
    std::vector<std::string> names_1 = {"Alice", "Bob", "Charlie", "Diana", "Eve"};
    std::vector<int32_t> values_1 = {25, 30, 35, 28, 32};
    std::vector<int32_t> ids_2 = {6, 7, 8, 9, 10};
    std::vector<std::string> names_2 = {"Frank", "Grace", "Heidi", "Ivan", "Judy"};
    std::vector<int32_t> values_2 = {60, 70, 80, 90, 100};

    std::cout << "[cpp]: << Creating batch Arrow data and write to table in append mode..." << std::endl;
    struct ArrowArrayStream write_stream;
    result = create_batch_arrow_stream(schema, ids_1, names_1, values_1, &write_stream);
    ASSERT_TRUE(result == 0, "Failed to create Arrow stream");
    result = lance_append_arrow_stream("users", &write_stream);
    ASSERT_TRUE(result == 0, "Failed to write Arrow stream data");

    std::cout << "[cpp]: << Reading data as Arrow stream..." << std::endl;
    struct ArrowArrayStream read_stream;
    result = lance_read_arrow_stream("users", &read_stream);
    ASSERT_TRUE(result == 0, "Failed to read Arrow stream data");
    display_arrow_stream(&read_stream);

    std::cout << "[cpp]: << Creating batch Arrow data and write to table in overwirte mode..." << std::endl;
    result = create_batch_arrow_stream(schema, ids_2, names_2, values_2, &write_stream);
    ASSERT_TRUE(result == 0, "Failed to create Arrow stream");
    result = lance_overwrite_arrow_stream("users", &write_stream);
    ASSERT_TRUE(result == 0, "Failed to write Arrow stream data");

    std::cout << "[cpp]: << Reading data as Arrow stream..." << std::endl;
    result = lance_read_arrow_stream("users", &read_stream);
    ASSERT_TRUE(result == 0, "Failed to read Arrow stream data");
    display_arrow_stream(&read_stream);

    std::cout << "[cpp]: << Creating stream Arrow data and write to table in append mode..." << std::endl;
    result = create_customized_arrow_stream(schema, ids_1, names_1, values_1, &write_stream);
    ASSERT_TRUE(result == 0, "Failed to create Arrow stream");
    result = lance_append_arrow_stream("users", &write_stream);
    ASSERT_TRUE(result == 0, "Failed to write Arrow stream data");

    std::cout << "[cpp]: << Reading data as Arrow stream..." << std::endl;
    result = lance_read_arrow_stream("users", &read_stream);
    ASSERT_TRUE(result == 0, "Failed to read Arrow stream data");
    display_arrow_stream(&read_stream);

    std::cout << "[cpp]: << Creating stream Arrow data and write to table in overwrite mode..." << std::endl;
    result = create_customized_arrow_stream(schema, ids_2, names_2, values_2, &write_stream);
    ASSERT_TRUE(result == 0, "Failed to create Arrow stream");
    result = lance_overwrite_arrow_stream("users", &write_stream);
    ASSERT_TRUE(result == 0, "Failed to write Arrow stream data");

    std::cout << "[cpp]: << Reading data as Arrow stream..." << std::endl;
    result = lance_read_arrow_stream("users", &read_stream);
    ASSERT_TRUE(result == 0, "Failed to read Arrow stream data");
    display_arrow_stream(&read_stream);

    std::cout << "[cpp]: << Cleanup lance resources..." << std::endl;
    lance_cleanup();

    return 0;
}
