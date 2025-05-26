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

void print_arrow_stream(ArrowArrayStream* stream) {
    ArrowSchema schema;

    ASSERT(stream->get_schema(stream, &schema) == 0, "Failed to get schema");
    std::cout << "schema.format: " << schema.format << std::endl;
    std::cout << "schema.n_children: " << schema.n_children << std::endl;
    for (int i = 0; i < schema.n_children; ++i) {
        std::cout << "    " << i << ": (" << schema.children[i]->name << ", " << schema.children[i]->format << ")"
                  << std::endl;
    }

    ArrowArray array;
    ASSERT(stream->get_next(stream, &array) == 0, "Failed to get next batch");

    const uint8_t* validity = static_cast<const uint8_t*>(array.children[0]->buffers[0]);
    const int32_t* values = static_cast<const int32_t*>(array.children[0]->buffers[1]);

    std::cout << "Cpp read values: " << std::endl;
    for (int64_t i = 0; i < array.length; ++i) {
        bool is_valid = true;
        if (array.null_count > 0 && validity) {
            is_valid = (validity[i / 8] >> (i % 8)) & 1;
        }

        if (is_valid) {
            std::cout << "  " << i << ": " << values[i] << std::endl;
        } else {
            std::cout << "  " << i << ": null" << std::endl;
        }
    }

    array.release(&array);
    schema.release(&schema);
}

void read_data_from_java_side() {
    std::cout << "============================= read_data_from_java_side =============================" << std::endl;
    ArrowArrayStream stream;

    using namespace jni_utils;
    auto* env = get_env();
    AutoLocalJobject jcls = find_class(env, "org/liuyehcf/ArrowStreamProvider");
    auto mid = get_mid(env, jcls, "generate", "(J)V", true);
    invoke_static_method(env, jcls, &mid, &stream);
    print_arrow_stream(&stream);
}

void write_data_to_java_side() {
    std::cout << "============================= write_data_to_java_side =============================" << std::endl;
    const std::vector<std::string> values = {"apple", "banana", "cherry", "date", "elderberry"};

    std::cout << "Cpp write values: " << std::endl;
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

int main() {
    init_jni_env();
    read_data_from_java_side();
    write_data_to_java_side();
    return 0;
}
