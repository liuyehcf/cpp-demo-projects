#include <jni_utils.h>

#include <algorithm>
#include <cstdlib>
#include <cstring>
#include <mutex>
#include <sstream>
#include <string>
#include <unordered_map>
#include <vector>

namespace jni_utils {

static constexpr auto CLASSPATH = "CLASSPATH";
static constexpr auto OPT_CLASSPATH = "-Djava.class.path=";
static constexpr auto JVM_ARGS = "JNI_OPS";

jclass jcls_list = nullptr;
jclass jcls_hash_map = nullptr;

static void init_common_classes(JNIEnv* env) {
    {
        jclass jcls = find_class(env, "java/util/List");
        jcls_list = static_cast<jclass>(env->NewGlobalRef(jcls));
        env->DeleteLocalRef(jcls);
    }
    {
        jclass jcls = find_class(env, "java/util/HashMap");
        jcls_hash_map = static_cast<jclass>(env->NewGlobalRef(jcls));
        env->DeleteLocalRef(jcls);
    }
}

static JNIEnv* getGlobalJNIEnv() {
    static constexpr auto VM_BUF_LENGTH = 1;
    JavaVM* vm_buf[VM_BUF_LENGTH];
    jint num_vms = 0;
    JavaVM* vm;
    JNIEnv* jni_env;

    jint rv = JNI_GetCreatedJavaVMs(&vm_buf[0], VM_BUF_LENGTH, &num_vms);
    if (rv != 0) {
        throw std::runtime_error("JNI_GetCreatedJavaVMs failed with error: " + std::to_string(rv));
    }

    if (num_vms == 0) {
        std::vector<std::string> options = {"-Djdk.lang.processReaperUseDefaultStackSize=true", "-Xrs"};

        char* class_path = getenv(CLASSPATH);
        if (class_path != nullptr) {
            std::string opt_class_path = OPT_CLASSPATH;
            opt_class_path.append(class_path);
            options.push_back(std::move(opt_class_path));
        } else {
            throw std::runtime_error("Environment variable CLASSPATH not set!");
        }

        char* jvm_args = getenv(JVM_ARGS);
        if (jvm_args != nullptr) {
            std::string opt_jvm_args = jvm_args;
            std::istringstream iss(opt_jvm_args);
            std::string option;
            while (std::getline(iss, option, ' ')) {
                options.push_back(option);
            }
        }

        JavaVMInitArgs vm_args;
        JavaVMOption vm_options[options.size()];
        for (size_t i = 0; i < options.size(); ++i) {
            vm_options[i].optionString = new char[options[i].size() + 1];
            std::strcpy(vm_options[i].optionString, options[i].c_str());
        }

        vm_args.version = JNI_VERSION_1_8;
        vm_args.options = vm_options;
        vm_args.nOptions = options.size();
        vm_args.ignoreUnrecognized = JNI_TRUE;

        rv = JNI_CreateJavaVM(&vm, reinterpret_cast<void**>(&jni_env), &vm_args);
        if (rv != 0) {
            throw std::runtime_error("JNI_CreateJavaVM failed with error: " + std::to_string(rv));
        }

        init_common_classes(jni_env);
    } else {
        vm = vm_buf[0];
        rv = vm->AttachCurrentThread(reinterpret_cast<void**>(&jni_env), nullptr);
        if (rv != 0) {
            throw std::runtime_error("AttachCurrentThread failed with error: " + std::to_string(rv));
        }
    }

    return jni_env;
}

static std::string get_jstack_trace(JNIEnv* env) {
    jthrowable jthr = env->ExceptionOccurred();
    env->ExceptionClear();

    // public static String getStackTrace(Throwable throwable) {
    //     StringWriter sw = new StringWriter();
    //     PrintWriter pw = new PrintWriter(sw, true);
    //     throwable.printStackTrace(pw);
    //     return sw.getBuffer().toString();
    // }
    AutoLocalJobject jcls_sw = find_class(env, "java/io/StringWriter");
    Method m_sw_ctor = get_mid(env, jcls_sw, "<init>", "()V", false);
    AutoLocalJobject jobj_sw = env->NewObject(jcls_sw, m_sw_ctor.mid);

    AutoLocalJobject jcls_pw = find_class(env, "java/io/PrintWriter");
    Method m_pw_ctor = get_mid(env, jcls_pw, "<init>", "(Ljava/io/Writer;)V", false);
    AutoLocalJobject jobj_pw = env->NewObject(jcls_pw, m_pw_ctor.mid, jobj_sw.get());

    AutoLocalJobject jcls_thr = env->GetObjectClass(jthr);
    Method m_print_stack_trace = get_mid(env, jcls_thr, "printStackTrace", "(Ljava/io/PrintWriter;)V", false);
    invoke_method(env, jthr, &m_print_stack_trace, jobj_pw.get());

    Method m_to_string = get_mid(env, jcls_sw, "toString", "()Ljava/lang/String;", false);
    AutoLocalJobject jstr_stack_strace = static_cast<jstring>(invoke_method(env, jobj_sw, &m_to_string).l);

    const char* stack_strace = env->GetStringUTFChars(jstr_stack_strace, nullptr);
    std::string str_stack_strace(stack_strace);
    env->ReleaseStringUTFChars(jstr_stack_strace, stack_strace);

    return str_stack_strace;
}

static thread_local JNIEnv* tls_env = nullptr;
JNIEnv* get_env() {
    static std::mutex init_mutex;
    if (tls_env == nullptr) {
        std::lock_guard lock(init_mutex);
        tls_env = getGlobalJNIEnv();
    }
    return tls_env;
}

jclass find_class(JNIEnv* env, const char* classname) {
    jclass jcls = env->FindClass(classname);
    if (env->ExceptionOccurred() != nullptr) {
        throw std::runtime_error("Failed to find class '" + std::string(classname) + "', Java Stack:\n" +
                                 get_jstack_trace(env));
    }
    return jcls;
}

Method get_mid(JNIEnv* env, jclass jcls, const char* name, const char* sig, bool is_static) {
    jmethodID mid;
    if (is_static) {
        mid = env->GetStaticMethodID(jcls, name, sig);
    } else {
        mid = env->GetMethodID(jcls, name, sig);
    }
    if (env->ExceptionOccurred() != nullptr) {
        throw std::runtime_error("Failed to get method '" + std::string(name) + "', Java Stack:\n" +
                                 get_jstack_trace(env));
    }
    return {mid, name, sig};
}

jvalue invoke_method(JNIEnv* env, jobject jobj, Method* method, ...) {
    va_list args;
    jvalue res;

    va_start(args, method);
    if (method->is_return_ref()) {
        res.l = env->CallObjectMethodV(jobj, method->mid, args);
    } else if (method->is_return_void()) {
        env->CallVoidMethodV(jobj, method->mid, args);
    } else if (method->is_return_boolean()) {
        res.z = env->CallBooleanMethodV(jobj, method->mid, args);
    } else if (method->is_return_byte()) {
        res.b = env->CallByteMethodV(jobj, method->mid, args);
    } else if (method->is_return_char()) {
        res.c = env->CallCharMethodV(jobj, method->mid, args);
    } else if (method->is_return_short()) {
        res.s = env->CallShortMethodV(jobj, method->mid, args);
    } else if (method->is_return_int()) {
        res.i = env->CallIntMethodV(jobj, method->mid, args);
    } else if (method->is_return_long()) {
        res.j = env->CallLongMethodV(jobj, method->mid, args);
    } else if (method->is_return_float()) {
        res.f = env->CallFloatMethodV(jobj, method->mid, args);
    } else if (method->is_return_double()) {
        res.d = env->CallDoubleMethodV(jobj, method->mid, args);
    }
    va_end(args);

    if (env->ExceptionOccurred() != nullptr) {
        throw std::runtime_error("Exception occurred while invoking object method '" + method->to_string() +
                                 "', Java Stack:\n" + get_jstack_trace(env));
    }
    return res;
}

jvalue invoke_static_method(JNIEnv* env, jclass jcls, Method* method, ...) {
    va_list args;
    jvalue res;

    va_start(args, method);
    if (method->is_return_ref()) {
        res.l = env->CallStaticObjectMethodV(jcls, method->mid, args);
    } else if (method->is_return_void()) {
        env->CallStaticVoidMethodV(jcls, method->mid, args);
    } else if (method->is_return_boolean()) {
        res.z = env->CallStaticBooleanMethodV(jcls, method->mid, args);
    } else if (method->is_return_byte()) {
        res.b = env->CallStaticByteMethodV(jcls, method->mid, args);
    } else if (method->is_return_char()) {
        res.c = env->CallStaticCharMethodV(jcls, method->mid, args);
    } else if (method->is_return_short()) {
        res.s = env->CallStaticShortMethodV(jcls, method->mid, args);
    } else if (method->is_return_int()) {
        res.i = env->CallStaticIntMethodV(jcls, method->mid, args);
    } else if (method->is_return_long()) {
        res.j = env->CallStaticLongMethodV(jcls, method->mid, args);
    } else if (method->is_return_float()) {
        res.f = env->CallStaticFloatMethodV(jcls, method->mid, args);
    } else if (method->is_return_double()) {
        res.d = env->CallStaticDoubleMethodV(jcls, method->mid, args);
    }
    va_end(args);

    if (env->ExceptionOccurred() != nullptr) {
        throw std::runtime_error("Exception occurred while invoking static method '" + method->to_string() +
                                 "', Java Stack:\n" + get_jstack_trace(env));
    }
    return res;
}

static std::string parse_type(const char* sig, size_t& index) {
    static std::unordered_map<char, std::string> TYPE_MAP = {
            {JBYTE, "byte"}, {JCHAR, "char"},   {JDOUBLE, "double"},   {JFLOAT, "float"}, {JINT, "int"},
            {JLONG, "long"}, {JSHORT, "short"}, {JBOOLEAN, "boolean"}, {JVOID, "void"},   {JOBJECT, "Object"}};
    char c_type = sig[index++];
    if (c_type == JOBJECT) {
        std::string objectType;
        while (sig[index] != ';') {
            objectType += sig[index++];
        }
        // skip the ';'
        ++index;
        std::replace(objectType.begin(), objectType.end(), '/', '.');
        return objectType;
    } else if (c_type == '[') {
        return parse_type(sig, index) + "[]";
    } else {
        return TYPE_MAP[c_type];
    }
}

std::string Method::to_string() const {
    size_t index = 0;
    if (sig[index++] != '(') {
        return "Invalid signature";
    }

    std::string buffer;

    std::string parameters;
    while (sig[index] != ')') {
        if (!parameters.empty()) {
            parameters += ", ";
        }
        parameters += parse_type(sig, index);
    }
    // skip the ')'
    ++index;

    buffer.append(parse_type(sig, index)).append(" ").append(name).append("(").append(parameters).append(")");

    return buffer;
}
} // namespace jni_utils
