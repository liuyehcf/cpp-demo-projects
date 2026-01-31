#include <arrow/api.h>
#include <arrow/c/abi.h>
#include <arrow/c/bridge.h>
#include <jni_utils.h>
#include <unistd.h>

#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <iostream>

#define ASSERT(expr, msg)                                      \
    if (!(expr)) {                                             \
        std::cerr << "Assertion failed: " << msg << std::endl; \
        std::exit(EXIT_FAILURE);                               \
    }

#define CHECK_ARROW_STATUS(status, msg)                            \
    if (!status.ok()) {                                            \
        std::cerr << msg << ": " << status.message() << std::endl; \
        return;                                                    \
    }

void init_jni_env() {
    std::string lib_dir = "";
    char result[1024];
    const std::filesystem::path self = (std::string(result, readlink("/proc/self/exe", result, 1024)));
    auto current_search_path = self.parent_path();
    while (current_search_path.has_parent_path() && current_search_path != current_search_path.root_path()) {
        std::cout << "Trying to find jni lib in " << current_search_path << std::endl;
        const auto test_path = current_search_path / "lib" / "jar";
        if (std::filesystem::exists(test_path) && std::filesystem::is_directory(test_path)) {
            lib_dir = test_path.string();
            std::cout << "Find jni lib in " << lib_dir << std::endl;
            break;
        }
        current_search_path = current_search_path.parent_path();
    }
    if (lib_dir.empty()) {
        std::cout << "Can't find jni lib in " << self << std::endl;
    }

    std::vector<std::filesystem::path> jar_files;
    for (const auto& entry : std::filesystem::recursive_directory_iterator(lib_dir)) {
        if (entry.is_regular_file() && entry.path().extension() == ".jar") {
            jar_files.push_back(entry.path());
        }
    }
    std::string classpath;
    if (!jar_files.empty()) {
        std::stringstream ss;
        std::copy(jar_files.begin(), jar_files.end() - 1, std::ostream_iterator<std::string>(ss, ":"));
        ss << jar_files.back().string();
        classpath = ss.str();
    }

    static constexpr auto ENV_REPLACE = 1;
    char* exist_classpath = std::getenv("CLASSPATH");
    if (!exist_classpath || strlen(exist_classpath) == 0) {
        setenv("CLASSPATH", classpath.c_str(), ENV_REPLACE);
        std::cout << "Set CLASSPATH=" << classpath << std::endl;
    } else {
        std::cout << "Existing CLASSPATH=" << exist_classpath << std::endl;
    }

    char* exist_jni_opt = std::getenv("JNI_OPS");
    const char* default_jni_opt = "--add-opens=java.base/java.nio=ALL-UNNAMED";
    if (!exist_jni_opt || strlen(exist_jni_opt) == 0) {
        setenv("JNI_OPS", default_jni_opt, ENV_REPLACE);
        std::cout << "Set JNI_OPS=" << default_jni_opt << std::endl;
    } else {
        std::cout << "Existing JNI_OPS=" << exist_jni_opt << std::endl;
    }
}

void print_arrow_stream(ArrowArrayStream* stream) {
    auto maybe_reader = arrow::ImportRecordBatchReader(stream);
    if (!maybe_reader.ok()) {
        std::cerr << "Failed to import RecordBatchReader: " << maybe_reader.status().ToString() << std::endl;
        return;
    }

    std::shared_ptr<arrow::RecordBatchReader> reader = *maybe_reader;

    std::shared_ptr<arrow::RecordBatch> batch;
    while (true) {
        CHECK_ARROW_STATUS(reader->ReadNext(&batch), "Failed to read");
        if (!batch) {
            // End of stream
            break;
        }

        std::cout << "[cpp] Read values:" << std::endl;
        // Pretty-print the batch to stdout
        arrow::PrettyPrintOptions print_options(/*indent=*/2);
        print_options.window = 100; // Control max values shown
        CHECK_ARROW_STATUS(arrow::PrettyPrint(*batch, print_options, &std::cout), "Failed to print");
        std::cout << std::endl;
    }
}

void read_data_from_java_side() {
    std::cout << "========================== read_data_from_java_side ==========================" << std::endl;
    ArrowArrayStream stream;
    memset(&stream, 0, sizeof(ArrowArrayStream));

    using namespace jni_utils;
    auto* env = get_env();
    AutoGlobalJobject jcls = find_class(env, "org/liuyehcf/ArrowStreamProvider");
    auto mid = get_method(env, jcls, "generate", "(J)V", true);
    invoke_static_method(env, jcls, &mid, &stream);
    print_arrow_stream(&stream);

    if (stream.release) stream.release(&stream);
}

void batch_write_data_to_java_side() {
    std::cout << "========================== batch_write_data_to_java_side ==========================" << std::endl;
    const std::vector<std::string> values = {"apple", "banana", "cherry", "date", "elderberry"};

    std::cout << "[cpp] Write values: " << std::endl;
    for (size_t i = 0; i < values.size(); ++i) {
        std::cout << "  " << i << ": " << values[i] << std::endl;
    }

    // Step 1: Create a StringArray using StringBuilder
    arrow::StringBuilder builder;
    auto status = builder.AppendValues(values);
    CHECK_ARROW_STATUS(status, "Failed to append values");

    std::shared_ptr<arrow::Array> string_array;
    status = builder.Finish(&string_array);
    CHECK_ARROW_STATUS(status, "Failed to finish StringArray");

    // Step 2: Define the schema
    auto field = arrow::field("col_str", arrow::utf8());
    auto schema = arrow::schema({field});

    // Step 3: Create a RecordBatch
    std::shared_ptr<arrow::RecordBatch> record_batch = arrow::RecordBatch::Make(schema, values.size(), {string_array});
    ASSERT(record_batch != nullptr, "Failed to create RecordBatch");

    // Step 4: Create a RecordBatchReader
    std::vector<std::shared_ptr<arrow::RecordBatch>> batches = {record_batch};
    auto batch_reader = arrow::RecordBatchReader::Make(batches).ValueOrDie();
    ASSERT(batch_reader != nullptr, "Failed to create RecordBatchReader");

    // Step 5: Export to ArrowArrayStream
    ArrowArrayStream stream;
    status = arrow::ExportRecordBatchReader(batch_reader, &stream);
    CHECK_ARROW_STATUS(status, "Failed to export RecordBatchReader");

    // Step 6: Pass to Java via JNI
    using namespace jni_utils;
    auto* env = get_env();
    AutoGlobalJobject jcls = find_class(env, "org/liuyehcf/ArrowStreamConsumer");
    auto mid = get_method(env, jcls, "consume", "(J)V", true);
    invoke_static_method(env, jcls, &mid, reinterpret_cast<ArrowArrayStream*>(&stream));

    if (stream.release) stream.release(&stream);
}

void stream_write_data_to_java_side() {
    class StreamingRecordBatchReader : public arrow::RecordBatchReader {
    public:
        StreamingRecordBatchReader() {
            _schema = arrow::schema({arrow::field(
                    "person", arrow::struct_({arrow::field("name", arrow::utf8()), arrow::field("age", arrow::int32()),
                                              arrow::field("active", arrow::boolean())}))});
        }

        std::shared_ptr<arrow::Schema> schema() const override { return _schema; }

        arrow::Status ReadNext(std::shared_ptr<arrow::RecordBatch>* batch) override {
            if (_batch_index >= _total_batches) {
                *batch = nullptr;
                return arrow::Status::OK();
            }

            std::cout << "[cpp] Generate batch " << _batch_index << std::endl;

            arrow::StringBuilder name_builder;
            arrow::Int32Builder age_builder;
            arrow::BooleanBuilder active_builder;

            for (size_t i = 0; i < _batch_size; ++i) {
                ARROW_RETURN_NOT_OK(name_builder.Append("User_" + std::to_string(_batch_index * _batch_size + i)));
                ARROW_RETURN_NOT_OK(age_builder.Append(20 + i));
                ARROW_RETURN_NOT_OK(active_builder.Append(i % 2 == 0));
            }

            std::shared_ptr<arrow::Array> name_array;
            std::shared_ptr<arrow::Array> age_array;
            std::shared_ptr<arrow::Array> active_array;

            ARROW_RETURN_NOT_OK(name_builder.Finish(&name_array));
            ARROW_RETURN_NOT_OK(age_builder.Finish(&age_array));
            ARROW_RETURN_NOT_OK(active_builder.Finish(&active_array));

            auto struct_type = std::dynamic_pointer_cast<arrow::StructType>(_schema->field(0)->type());
            auto struct_array = std::make_shared<arrow::StructArray>(
                    struct_type, name_array->length(),
                    std::vector<std::shared_ptr<arrow::Array>>{name_array, age_array, active_array});

            std::cout << "[cpp] Batch values: " << struct_array->ToString() << std::endl;

            *batch = arrow::RecordBatch::Make(_schema, struct_array->length(), {struct_array});

            _batch_index++;
            return arrow::Status::OK();
        }

    private:
        const size_t _batch_size = 2;
        size_t _batch_index = 0;
        const size_t _total_batches = 3;
        std::shared_ptr<arrow::Schema> _schema;
    };
    std::cout << "========================== stream_write_data_to_java_side ==========================" << std::endl;

    auto reader = std::make_shared<StreamingRecordBatchReader>();

    // Export to ArrowArrayStream
    ArrowArrayStream stream;
    auto status = arrow::ExportRecordBatchReader(reader, &stream);
    CHECK_ARROW_STATUS(status, "Failed to export streaming RecordBatchReader");

    // Pass to Java via JNI
    using namespace jni_utils;
    auto* env = get_env();
    AutoGlobalJobject jcls = find_class(env, "org/liuyehcf/ArrowStreamConsumer");
    auto mid = get_method(env, jcls, "consume", "(J)V", true);
    invoke_static_method(env, jcls, &mid, reinterpret_cast<ArrowArrayStream*>(&stream));

    if (stream.release) stream.release(&stream);
}

int main() {
    init_jni_env();
    read_data_from_java_side();
    batch_write_data_to_java_side();
    stream_write_data_to_java_side();
    return 0;
}
