#include <jni.h>

#include <stdexcept>

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
    Method(jmethodID mid_, const char* name_, const char* sig_) : mid(mid_), name(name_), sig(sig_) {
        const char* str = sig;
        while (*str != ')') str++;
        str++;
        const_cast<char&>(return_type) = *str;
    }
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

    const jmethodID mid;
    const char* const name;
    const char* const sig;
    const char return_type = '\0';
};

JNIEnv* get_env();
jclass find_class(JNIEnv* env, const char* classname);
Method get_mid(JNIEnv* env, jclass jcls, const char* name, const char* sig, bool is_static);
jvalue invoke_method(JNIEnv* env, jobject jobj, Method* method, ...);
jvalue invoke_static_method(JNIEnv* env, jclass jcls, Method* method, ...);

enum RefType {
    LOCAL,
    GLOBAL,
    WEAK_GLOBAL,
};

template <RefType ref_type>
class AutoJobject {
public:
    AutoJobject(jobject obj) : _obj(obj) {}
    AutoJobject() : _obj(nullptr) {}
    AutoJobject(const AutoJobject&) = delete;
    AutoJobject(AutoJobject&&) = delete;
    AutoJobject& operator=(const AutoJobject&) = default;
    AutoJobject& operator=(AutoJobject&&) = default;
    ~AutoJobject() { release(); }

    jobject get() { return _obj; }
    operator jobject() const { return _obj; }
    operator jclass() const { return static_cast<jclass>(_obj); }
    operator jarray() const { return static_cast<jarray>(_obj); }
    operator jstring() const { return static_cast<jstring>(_obj); }
    operator jthrowable() const { return static_cast<jthrowable>(_obj); }

private:
    void release() {
        if constexpr (RefType::LOCAL == ref_type) {
            if (_obj != nullptr) get_env()->DeleteLocalRef(_obj);
        } else if constexpr (RefType::GLOBAL == ref_type) {
            if (_obj != nullptr) get_env()->DeleteGlobalRef(_obj);
        } else if constexpr (RefType::WEAK_GLOBAL == ref_type) {
            if (_obj != nullptr) get_env()->DeleteWeakGlobalRef(_obj);
        }
    }
    jobject _obj;
};

using AutoLocalJobject = AutoJobject<RefType::LOCAL>;
using AutoGlobalJobject = AutoJobject<RefType::GLOBAL>;
using AutoWeakGlobalJobject = AutoJobject<RefType::WEAK_GLOBAL>;

} // namespace jni_utils