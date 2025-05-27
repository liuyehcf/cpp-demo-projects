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
    AutoLocalJobject jcls = find_class(env, "org/liuyehcf/ArrowStreamProvider");
    auto mid = get_mid(env, jcls, "generate", "(J)V", true);
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
    AutoLocalJobject jcls = find_class(env, "org/liuyehcf/ArrowStreamConsumer");
    auto mid = get_mid(env, jcls, "consume", "(J)V", true);
    invoke_static_method(env, jcls, &mid, reinterpret_cast<ArrowArrayStream*>(&stream));

    if (stream.release) stream.release(&stream);
}

void stream_write_data_to_java_side() {
    class StreamingRecordBatchReader : public arrow::RecordBatchReader {
    public:
        explicit StreamingRecordBatchReader()
                : schema_(arrow::schema({arrow::field("col_str", arrow::utf8())})), finished_(false) {}
        std::shared_ptr<arrow::Schema> schema() const override { return schema_; }

        arrow::Status ReadNext(std::shared_ptr<arrow::RecordBatch>* out) override {
            if (finished_) {
                *out = nullptr; // No more data
                return arrow::Status::OK();
            }

            // Simulate receiving the next chunk of data
            std::vector<std::string> next_chunk = GetNextBatch();
            if (next_chunk.empty()) {
                finished_ = true;
                *out = nullptr;
                return arrow::Status::OK();
            }

            // Convert strings to Arrow array
            arrow::StringBuilder builder;
            ARROW_RETURN_NOT_OK(builder.AppendValues(next_chunk));

            std::shared_ptr<arrow::Array> array;
            ARROW_RETURN_NOT_OK(builder.Finish(&array));

            *out = arrow::RecordBatch::Make(schema_, next_chunk.size(), {array});
            return arrow::Status::OK();
        }

    private:
        std::shared_ptr<arrow::Schema> schema_;
        bool finished_;

        std::vector<std::string> GetNextBatch() {
            static int call_count = 0;
            if (call_count == 0) {
                ++call_count;
                std::cout << "[cpp] Generate first batch" << std::endl;
                return {"streamed_apple", "streamed_banana"};
            } else if (call_count == 1) {
                ++call_count;
                std::cout << "[cpp] Generate second batch" << std::endl;
                return {"streamed_cherry"};
            } else if (call_count == 2) {
                ++call_count;
                std::cout << "[cpp] Generate third batch" << std::endl;
                return {"streamed_date", "streamed_elderberry"};
            }
            return {};
        }
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
    AutoLocalJobject jcls = find_class(env, "org/liuyehcf/ArrowStreamConsumer");
    auto mid = get_mid(env, jcls, "consume", "(J)V", true);
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
