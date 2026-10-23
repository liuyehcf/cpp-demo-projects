#include <jni.h>

#include <cstdint>
#include <map>
#include <optional>
#include <string>
#include <unordered_map>
#include <vector>

namespace jni_utils {

#define JVOID 'V'
#define JOBJECT 'L'
#define JARRAYOBJECT '['
#define JBOOLEAN 'Z'
#define JBYTE 'B'
#define JCHAR 'C'
#define JSHORT 'S'
#define JINT 'I'
#define JLONG 'J'
#define JFLOAT 'F'
#define JDOUBLE 'D'

struct Method {
    Method(jmethodID jmid_, const char* name_, const char* signature_)
            : jmid(jmid_), name(name_), signature(signature_) {
        const char* str = signature;
        while (*str != ')') {
            str++;
        }
        str++;
        return_type = *str;
    }
    Method() = default;
    Method(const Method&) = default;
    Method& operator=(const Method&) = default;
    ~Method() = default;

    bool is_return_ref() const { return return_type == JOBJECT || return_type == JARRAYOBJECT; }
    bool is_return_void() const { return return_type == JVOID; }
    bool is_return_object() const { return return_type == JOBJECT; }
    bool is_return_array() const { return return_type == JARRAYOBJECT; }
    bool is_return_boolean() const { return return_type == JBOOLEAN; }
    bool is_return_byte() const { return return_type == JBYTE; }
    bool is_return_char() const { return return_type == JCHAR; }
    bool is_return_short() const { return return_type == JSHORT; }
    bool is_return_int() const { return return_type == JINT; }
    bool is_return_long() const { return return_type == JLONG; }
    bool is_return_float() const { return return_type == JFLOAT; }
    bool is_return_double() const { return return_type == JDOUBLE; }
    std::string to_string() const;

    jmethodID jmid = nullptr;
    const char* name = nullptr;
    const char* signature = nullptr;
    char return_type = '\0';
};

// Basic methods
JNIEnv* get_env();
bool is_jvm_created();

namespace raw {

jthrowable _find_class(JNIEnv* env, jclass* jcls, const char* class_name);
jthrowable _find_method_id(JNIEnv* env, jmethodID* jmid, jclass jcls, const char* method_name,
                           const char* method_signature, bool static_method);
jthrowable _invoke_object_method(JNIEnv* env, jvalue* jretval, jobject jobj, Method* method, ...);
jthrowable _invoke_object_methodV(JNIEnv* env, jvalue* jretval, jobject jobj, Method* method, va_list args);
jthrowable _invoke_static_method(JNIEnv* env, jvalue* jretval, jclass jcls, Method* method, ...);
jthrowable _invoke_static_methodV(JNIEnv* env, jvalue* jretval, jclass jcls, Method* method, va_list args);
jthrowable _invoke_new_object(JNIEnv* env, jobject* jobj, jclass jcls, Method* method, ...);
jthrowable _invoke_new_objectV(JNIEnv* env, jobject* jobj, jclass jcls, Method* method, va_list args);

std::string _get_exception_message(JNIEnv* env, jthrowable jthr);
std::string _get_jstack_trace(JNIEnv* env, jthrowable jthr);

}; // namespace raw

jclass find_class(JNIEnv* env, const char* class_name);
Method get_method(JNIEnv* env, jclass jcls, const char* method_name, const char* method_signature, bool is_static);
jvalue invoke_object_method(JNIEnv* env, jobject jobj, Method* method, ...);
jvalue invoke_static_method(JNIEnv* env, jclass jcls, Method* method, ...);
jobject invoke_new_object(JNIEnv* env, jclass jcls, Method* method, ...);
jobject new_global_ref(JNIEnv* env, jobject jobj);
jclass get_object_class(JNIEnv* env, jobject jobj);

// Util methods
int32_t jinteger_to_int32(JNIEnv* env, jobject jobj);
int64_t jlong_to_int64(JNIEnv* env, jstring jobj);
std::string jstr_to_str(JNIEnv* env, jstring jstr);                                 // java.lang.String to std::string
std::string jbytes_to_str(JNIEnv* env, jbyteArray jobj);                            // byte[] to std::string
jobject new_jbytes(JNIEnv* env, const char* data, size_t size);                     // create byte[]
jobject get_from_jmap(JNIEnv* env, jobject jmap, const std::string& key);           // java.util.Map get method
jobject map_to_jmap(JNIEnv* env, const std::map<std::string, std::string>& params); // std::map to java.util.Map
jobject vstrs_to_jlstrs(
        JNIEnv* env,
        const std::vector<std::string>& vec); // std::vector<std::string> to java.util.List<java.lang.String>
std::vector<std::string> jlbytes_to_vstrs(JNIEnv* env, jobject jlist);
std::vector<std::string> jlstrs_to_vstrs(JNIEnv* env, jobject jlist);
std::string get_exception_summary(JNIEnv* env, jthrowable jthr);
std::string get_stack_trace(JNIEnv* env, jthrowable jthr);
bool is_out_of_memory_error(JNIEnv* env, jthrowable jthr);
jstring new_jstr(JNIEnv* env, const std::string& str);
std::vector<std::string> jstrs_to_vstrs(JNIEnv* env, jobjectArray jstrs);

enum RefType {
    LOCAL,
    GLOBAL,
    WEAK_GLOBAL,
};

template <RefType ref_type>
class AutoJobject {
public:
    // NOLINTBEGIN(google-explicit-constructor, google-runtime-int)
    AutoJobject(jobject jobj) : _jobj(jobj) {}
    AutoJobject() : _jobj(nullptr) {}
    AutoJobject(const AutoJobject&) = delete;
    AutoJobject(AutoJobject&&) = delete;
    AutoJobject& operator=(const AutoJobject&) = delete;
    AutoJobject& operator=(AutoJobject&&) = delete;
    AutoJobject& operator=(jobject jobj) {
        release();
        _jobj = jobj;
        return *this;
    }
    ~AutoJobject() { release(); }

    void* address() { return &_jobj; }
    jobject get() { return _jobj; }
    operator bool() const { return _jobj != nullptr; }
    operator jobject() const { return _jobj; }
    operator jclass() const { return static_cast<jclass>(_jobj); }
    operator jthrowable() const { return static_cast<jthrowable>(_jobj); }
    operator jstring() const { return static_cast<jstring>(_jobj); }
    operator jarray() const { return static_cast<jarray>(_jobj); }
    operator jbooleanArray() const { return static_cast<jbooleanArray>(_jobj); }
    operator jbyteArray() const { return static_cast<jbyteArray>(_jobj); }
    operator jcharArray() const { return static_cast<jcharArray>(_jobj); }
    operator jshortArray() const { return static_cast<jshortArray>(_jobj); }
    operator jintArray() const { return static_cast<jintArray>(_jobj); }
    operator jlongArray() const { return static_cast<jlongArray>(_jobj); }
    operator jfloatArray() const { return static_cast<jfloatArray>(_jobj); }
    operator jdoubleArray() const { return static_cast<jdoubleArray>(_jobj); }
    operator jobjectArray() const { return static_cast<jobjectArray>(_jobj); }
    // NOLINTEND(google-explicit-constructor, google-runtime-int)

private:
    void release() noexcept {
        if (_jobj == nullptr) {
            return;
        }

        try {
            if constexpr (RefType::LOCAL == ref_type) {
                get_env()->DeleteLocalRef(_jobj);
            } else if constexpr (RefType::GLOBAL == ref_type) {
                get_env()->DeleteGlobalRef(_jobj);
            } else if constexpr (RefType::WEAK_GLOBAL == ref_type) {
                get_env()->DeleteWeakGlobalRef(_jobj);
            }
        } catch (...) {
            // Reference cleanup is best effort and must not throw from a destructor.
        }

        _jobj = nullptr;
    }
    jobject _jobj;
};

using AutoLocalJobject = AutoJobject<RefType::LOCAL>;
using AutoGlobalJobject = AutoJobject<RefType::GLOBAL>;
using AutoWeakGlobalJobject = AutoJobject<RefType::WEAK_GLOBAL>;

class MemoryMonitor {
public:
    struct MemoryUsage {
        int64_t init;
        int64_t used;
        int64_t committed;
        int64_t max;
    };

    struct GarbageCollectorMetrics {
        std::string name;
        int64_t collection_count;
        int64_t collection_time_ms;
    };

    struct ThreadMetrics {
        int64_t count;
        int64_t peak_count;
        int64_t daemon_count;
        int64_t total_started_count;
    };

    struct ClassLoadingMetrics {
        int64_t loaded_count;
        int64_t total_loaded_count;
        int64_t unloaded_count;
    };

    MemoryMonitor();
    static MemoryMonitor& instance();

    MemoryUsage get_heap_memory_usage();
    MemoryUsage get_nonheap_memory_usage();
    int64_t get_uptime_milliseconds();
    std::vector<GarbageCollectorMetrics> get_garbage_collector_metrics();
    ThreadMetrics get_thread_metrics();
    ClassLoadingMetrics get_class_loading_metrics();
    std::optional<int64_t> get_compilation_time_milliseconds();

    void trigger_gc();

private:
    MemoryUsage to_memory_usage(JNIEnv* env, jobject jobj_memory_usage);

    AutoGlobalJobject jcls_management_factory;
    AutoGlobalJobject jcls_memory_mxbean;
    AutoGlobalJobject jcls_memory_usage;
    AutoGlobalJobject jcls_buffer_pool_mxbean;
    AutoGlobalJobject jcls_runtime_mxbean;
    AutoGlobalJobject jcls_garbage_collector_mxbean;
    AutoGlobalJobject jcls_thread_mxbean;
    AutoGlobalJobject jcls_class_loading_mxbean;
    AutoGlobalJobject jcls_compilation_mxbean;
    AutoGlobalJobject jcls_list;
    AutoGlobalJobject jcls_system;

    Method m_get_memory_mxbean;
    Method m_get_platform_mxbeans;
    Method m_get_runtime_mxbean;
    Method m_get_garbage_collector_mxbeans;
    Method m_get_thread_mxbean;
    Method m_get_class_loading_mxbean;
    Method m_get_compilation_mxbean;
    Method m_get_heap_memory_usage;
    Method m_get_non_heap_memory_usage;
    Method m_get_init;
    Method m_get_used;
    Method m_get_committed;
    Method m_get_max;
    Method m_get_buffer_pool_memory_used;
    Method m_get_uptime;
    Method m_get_garbage_collector_name;
    Method m_get_collection_count;
    Method m_get_collection_time;
    Method m_get_thread_count;
    Method m_get_peak_thread_count;
    Method m_get_daemon_thread_count;
    Method m_get_total_started_thread_count;
    Method m_get_loaded_class_count;
    Method m_get_total_loaded_class_count;
    Method m_get_unloaded_class_count;
    Method m_is_compilation_time_monitoring_supported;
    Method m_get_total_compilation_time;
    Method m_list_size;
    Method m_list_get;
    Method m_gc;

    AutoGlobalJobject jobj_memory_mxbean;
    AutoGlobalJobject jobj_runtime_mxbean;
    AutoGlobalJobject jobj_thread_mxbean;
    AutoGlobalJobject jobj_class_loading_mxbean;
    AutoGlobalJobject jobj_compilation_mxbean;
};

// Concat x and y
#define TOKEN_CONCAT(x, y) x##y
// Make sure x and y are fully expanded
#define TOKEN_CONCAT_FORWARD(x, y) TOKEN_CONCAT(x, y)

#define CHECK_JNI_EXCEPTION(expr, errorMsg)                                        \
    do {                                                                           \
        jni_utils::AutoLocalJobject TOKEN_CONCAT_FORWARD(jthr, __LINE__) = (expr); \
        if ((TOKEN_CONCAT_FORWARD(jthr, __LINE__)).operator bool()) {              \
            throw std::runtime_error(errorMsg);                                    \
        }                                                                          \
    } while (false)

#define THROW_JNI_EXCEPTION(env, expr)                                                                        \
    do {                                                                                                      \
        jni_utils::AutoLocalJobject TOKEN_CONCAT_FORWARD(jthr, __LINE__) = (expr);                            \
        if ((TOKEN_CONCAT_FORWARD(jthr, __LINE__)).operator bool()) {                                         \
            if (jni_utils::is_out_of_memory_error((env), (TOKEN_CONCAT_FORWARD(jthr, __LINE__)))) {           \
                throw std::runtime_error("Receive JNI OutOfMemoryError");                                     \
            }                                                                                                 \
            throw std::runtime_error(                                                                         \
                    "Receive JNI exception, message: " +                                                      \
                    jni_utils::get_exception_summary((env), (TOKEN_CONCAT_FORWARD(jthr, __LINE__))) +         \
                    ", stack: " + jni_utils::get_stack_trace((env), (TOKEN_CONCAT_FORWARD(jthr, __LINE__)))); \
        }                                                                                                     \
    } while (false)

} // namespace jni_utils
