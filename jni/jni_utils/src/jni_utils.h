#include <jni.h>

#include <map>
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

// Util methods
std::string jstr_to_str(JNIEnv* env, jstring jstr);                                 // std::string to java.lang.String
std::string jbytes_to_str(JNIEnv* env, jbyteArray obj);                             // byte[] to std::string
jobject new_jbytes(JNIEnv* env, const char* data, size_t size);                     // create byte[]
jobject get_from_jmap(JNIEnv* env, jobject jmap, const std::string& key);           // java.util.Map get method
jobject map_to_jmap(JNIEnv* env, const std::map<std::string, std::string>& params); // std::map to java.util.Map
jobject vstrs_to_jlstrs(
        JNIEnv* env,
        const std::vector<std::string>& vec); // std::vector<std::string> to java.util.List<java.lang.String>

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
    void release() {
        if (_jobj == nullptr) {
            return;
        }
        if constexpr (RefType::LOCAL == ref_type) {
            get_env()->DeleteLocalRef(_jobj);
        } else if constexpr (RefType::GLOBAL == ref_type) {
            get_env()->DeleteGlobalRef(_jobj);
        } else if constexpr (RefType::WEAK_GLOBAL == ref_type) {
            get_env()->DeleteWeakGlobalRef(_jobj);
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
    MemoryMonitor();
    static MemoryMonitor& instance();

    MemoryUsage get_heap_memory_usage();
    MemoryUsage get_nonheap_memory_usage();

private:
    MemoryUsage to_memory_usage(JNIEnv* env, jobject obj_memory_usage);

    AutoGlobalJobject jcls_management_factory;
    AutoGlobalJobject jcls_memory_mxbean;
    AutoGlobalJobject jcls_memory_usage;

    Method m_get_memory_mxbean;
    Method m_get_heap_memory_usage;
    Method m_get_non_heap_memory_usage;
    Method m_get_init;
    Method m_get_used;
    Method m_get_committed;
    Method m_get_max;

    AutoGlobalJobject obj_memory_mxbean;
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

#define THROW_JNI_EXCEPTION(env, expr)                                                                               \
    do {                                                                                                             \
        jni_utils::AutoLocalJobject TOKEN_CONCAT_FORWARD(jthr, __LINE__) = (expr);                                   \
        if ((TOKEN_CONCAT_FORWARD(jthr, __LINE__)).operator bool()) {                                                \
            throw std::runtime_error(                                                                                \
                    "Receive JNI exception, message: " +                                                             \
                    jni_utils::raw::_get_exception_message((env), (TOKEN_CONCAT_FORWARD(jthr, __LINE__))) +          \
                    ", stack: " + jni_utils::raw::_get_jstack_trace((env), (TOKEN_CONCAT_FORWARD(jthr, __LINE__)))); \
        }                                                                                                            \
    } while (false)

} // namespace jni_utils
