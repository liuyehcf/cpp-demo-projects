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

jclass jcls_class = nullptr;
jclass jcls_list = nullptr;
jclass jcls_arraylist = nullptr;
jclass jcls_hashmap = nullptr;

static void init_common_classes(JNIEnv* env) {
    {
        jclass jcls = find_class(env, "java/lang/Class");
        jcls_class = jcls;
    }
    {
        jclass jcls = find_class(env, "java/util/List");
        jcls_list = jcls;
    }
    {
        jclass jcls = find_class(env, "java/util/ArrayList");
        jcls_arraylist = jcls;
    }
    {
        jclass jcls = find_class(env, "java/util/HashMap");
        jcls_hashmap = jcls;
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
    } else {
        vm = vm_buf[0];
        rv = vm->AttachCurrentThread(reinterpret_cast<void**>(&jni_env), nullptr);
        if (rv != 0) {
            throw std::runtime_error("AttachCurrentThread failed with error: " + std::to_string(rv));
        }
    }

    // Ensure common classes are initialized exactly once regardless of JVM path
    static std::once_flag init_once;
    std::call_once(init_once, [&]() { init_common_classes(jni_env); });
    return jni_env;
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

namespace raw {

static jthrowable get_pending_exception_and_clear(JNIEnv* env) {
    jthrowable jthr = env->ExceptionOccurred();
    if (jthr == nullptr) return jthr;

    // env->ExceptionDescribe();
    env->ExceptionClear();
    return jthr;
}

jthrowable _find_class(JNIEnv* env, jclass* jcls, const char* class_name) {
    jthrowable jthr = nullptr;
    jclass jcls_global = nullptr;
    jclass jcls_local = nullptr;
    do {
        jcls_local = env->FindClass(class_name);
        if (!jcls_local) {
            jthr = get_pending_exception_and_clear(env);
            break;
        }
        jcls_global = static_cast<jclass>(env->NewGlobalRef(jcls_local));
        if (!jcls_global) {
            jthr = get_pending_exception_and_clear(env);
            break;
        }
        *jcls = jcls_global;
        jthr = nullptr;
    } while (false);

    if (jthr && jcls_global) {
        env->DeleteGlobalRef(jcls_global);
    }
    if (jcls_local) {
        env->DeleteLocalRef(jcls_local);
    }
    return jthr;
}

jthrowable _find_method_id(JNIEnv* env, jmethodID* jmid, jclass jcls, const char* method_name,
                           const char* method_signature, bool static_method) {
    if (static_method)
        *jmid = env->GetStaticMethodID(jcls, method_name, method_signature);
    else
        *jmid = env->GetMethodID(jcls, method_name, method_signature);
    if (*jmid == nullptr) return get_pending_exception_and_clear(env);
    return nullptr;
}

jthrowable _invoke_object_method(JNIEnv* env, jvalue* jretval, jobject jobj, Method* method, ...) {
    va_list args;

    va_start(args, method);
    jthrowable jthr = _invoke_object_methodV(env, jretval, jobj, method, args);
    va_end(args);

    return jthr;
}

jthrowable _invoke_object_methodV(JNIEnv* env, jvalue* jretval, jobject jobj, Method* method, va_list args) {
    if (method->is_return_ref()) {
        jretval->l = env->CallObjectMethodV(jobj, method->jmid, args);
    } else if (method->is_return_void()) {
        env->CallVoidMethodV(jobj, method->jmid, args);
    } else if (method->is_return_boolean()) {
        jretval->z = env->CallBooleanMethodV(jobj, method->jmid, args);
    } else if (method->is_return_byte()) {
        jretval->b = env->CallByteMethodV(jobj, method->jmid, args);
    } else if (method->is_return_char()) {
        jretval->c = env->CallCharMethodV(jobj, method->jmid, args);
    } else if (method->is_return_short()) {
        jretval->s = env->CallShortMethodV(jobj, method->jmid, args);
    } else if (method->is_return_int()) {
        jretval->i = env->CallIntMethodV(jobj, method->jmid, args);
    } else if (method->is_return_long()) {
        jretval->j = env->CallLongMethodV(jobj, method->jmid, args);
    } else if (method->is_return_float()) {
        jretval->f = env->CallFloatMethodV(jobj, method->jmid, args);
    } else if (method->is_return_double()) {
        jretval->d = env->CallDoubleMethodV(jobj, method->jmid, args);
    }
    return get_pending_exception_and_clear(env);
}

jthrowable _invoke_static_method(JNIEnv* env, jvalue* jretval, jclass jcls, Method* method, ...) {
    va_list args;

    va_start(args, method);
    jthrowable jthr = _invoke_static_methodV(env, jretval, jcls, method, args);
    va_end(args);

    return jthr;
}

jthrowable _invoke_static_methodV(JNIEnv* env, jvalue* jretval, jclass jcls, Method* method, va_list args) {
    if (method->is_return_ref()) {
        jretval->l = env->CallStaticObjectMethodV(jcls, method->jmid, args);
    } else if (method->is_return_void()) {
        env->CallStaticVoidMethodV(jcls, method->jmid, args);
    } else if (method->is_return_boolean()) {
        jretval->z = env->CallStaticBooleanMethodV(jcls, method->jmid, args);
    } else if (method->is_return_byte()) {
        jretval->b = env->CallStaticByteMethodV(jcls, method->jmid, args);
    } else if (method->is_return_char()) {
        jretval->c = env->CallStaticCharMethodV(jcls, method->jmid, args);
    } else if (method->is_return_short()) {
        jretval->s = env->CallStaticShortMethodV(jcls, method->jmid, args);
    } else if (method->is_return_int()) {
        jretval->i = env->CallStaticIntMethodV(jcls, method->jmid, args);
    } else if (method->is_return_long()) {
        jretval->j = env->CallStaticLongMethodV(jcls, method->jmid, args);
    } else if (method->is_return_float()) {
        jretval->f = env->CallStaticFloatMethodV(jcls, method->jmid, args);
    } else if (method->is_return_double()) {
        jretval->d = env->CallStaticDoubleMethodV(jcls, method->jmid, args);
    }
    return get_pending_exception_and_clear(env);
}

jthrowable _invoke_new_object(JNIEnv* env, jobject* jobj, jclass jcls, Method* method, ...) {
    va_list args;

    va_start(args, method);
    jthrowable jthr = _invoke_new_objectV(env, jobj, jcls, method, args);
    va_end(args);

    return jthr;
}

jthrowable _invoke_new_objectV(JNIEnv* env, jobject* jobj, jclass jcls, Method* method, va_list args) {
    *jobj = env->NewObjectV(jcls, method->jmid, args);
    return get_pending_exception_and_clear(env);
}

std::string _get_exception_message(JNIEnv* env, jthrowable jthr) {
    AutoLocalJobject jcls_execption = env->GetObjectClass(jthr);
    jmethodID jmid_get_message;
    const char* method_name_get_message = "getMessage";
    const char* signature_get_message = "()Ljava/lang/String;";
    if (_find_method_id(env, &jmid_get_message, jcls_execption, method_name_get_message, signature_get_message, false))
        return "Cannot find getMessage method";

    Method m_get_message = {jmid_get_message, method_name_get_message, signature_get_message};

    jvalue jretval;
    if (AutoLocalJobject jthr_get_message = _invoke_object_method(env, &jretval, jthr, &m_get_message);
        jthr_get_message)
        return "Failed to get exception message";

    AutoLocalJobject jstr = jretval.l;
    return jstr_to_str(env, jstr);
}

std::string _get_jstack_trace(JNIEnv* env, jthrowable jthr) {
    // public static String getStackTrace(Throwable throwable) {
    //     StringWriter sw = new StringWriter();
    //     PrintWriter pw = new PrintWriter(sw, true);
    //     throwable.printStackTrace(pw);
    //     return sw.getBuffer().toString();
    // }
    AutoLocalJobject jcls_execption = env->GetObjectClass(jthr);
    AutoLocalJobject jcls_sw;
    if (raw::_find_class(env, static_cast<jclass*>(jcls_sw.address()), "java/io/StringWriter"))
        return "Cannot find StringWriter class";
    jmethodID jmid_sw_ctor;
    if (raw::_find_method_id(env, &jmid_sw_ctor, jcls_sw, "<init>", "()V", false))
        return "Cannot find StringWriter constructor";
    Method m_sw_ctor = {jmid_sw_ctor, "<init>", "()V"};
    AutoLocalJobject jobj_sw;
    if (raw::_invoke_new_object(env, reinterpret_cast<jobject*>(jobj_sw.address()), jcls_sw, &m_sw_ctor))
        return "Failed to create StringWriter instance";

    AutoLocalJobject jcls_pw;
    if (raw::_find_class(env, static_cast<jclass*>(jcls_pw.address()), "java/io/PrintWriter"))
        return "Cannot find PrintWriter class";
    jmethodID jmid_pw_ctor;
    if (raw::_find_method_id(env, &jmid_pw_ctor, jcls_pw, "<init>", "(Ljava/io/Writer;)V", false))
        return "Cannot find PrintWriter constructor";
    Method m_pw_ctor = {jmid_pw_ctor, "<init>", "(Ljava/io/Writer;)V"};
    AutoLocalJobject jobj_pw;
    if (raw::_invoke_new_object(env, reinterpret_cast<jobject*>(jobj_pw.address()), jcls_pw, &m_pw_ctor, jobj_sw.get()))
        return "Failed to create PrintWriter instance";

    AutoLocalJobject jcls_thr = env->GetObjectClass(jthr);
    jmethodID jmid_print_stack_trace;
    if (raw::_find_method_id(env, &jmid_print_stack_trace, jcls_thr, "printStackTrace", "(Ljava/io/PrintWriter;)V",
                             false))
        return "Cannot find printStackTrace method";
    Method m_print_stack_trace = {jmid_print_stack_trace, "printStackTrace", "(Ljava/io/PrintWriter;)V"};
    {
        jvalue jretval;
        if (raw::_invoke_object_method(env, &jretval, jthr, &m_print_stack_trace, jobj_pw.get()))
            return "Failed to invoke printStackTrace method";
    }

    jmethodID jmid_to_string;
    if (raw::_find_method_id(env, &jmid_to_string, jcls_sw, "toString", "()Ljava/lang/String;", false))
        return "Cannot find toString method of StringWriter";
    Method m_to_string = {jmid_to_string, "toString", "()Ljava/lang/String;"};
    AutoLocalJobject jstr_stack_strace;
    {
        jvalue jretval;
        if (raw::_invoke_object_method(env, &jretval, jobj_sw, &m_to_string))
            return "Failed to invoke toString method of StringWriter";
        jstr_stack_strace = jretval.l;
    }

    const char* stack_strace = env->GetStringUTFChars(jstr_stack_strace, nullptr);
    std::string str_stack_strace(stack_strace);
    env->ReleaseStringUTFChars(jstr_stack_strace, stack_strace);

    return str_stack_strace;
}
} // namespace raw

jclass find_class(JNIEnv* env, const char* class_name) {
    jclass jcls = nullptr;
    CHECK_JNI_EXCEPTION(raw::_find_class(env, &jcls, class_name), "Cannot find class " + std::string(class_name));
    return jcls;
}

Method get_method(JNIEnv* env, jclass jcls, const char* method_name, const char* method_signature, bool is_static) {
    jmethodID jmid = nullptr;
    CHECK_JNI_EXCEPTION(raw::_find_method_id(env, &jmid, jcls, method_name, method_signature, is_static),
                        "Cannot find method " + std::string(method_name));
    return {jmid, method_name, method_signature};
}

jvalue invoke_object_method(JNIEnv* env, jobject jobj, Method* method, ...) {
    va_list args;

    va_start(args, method);
    jvalue jretval;
    THROW_JNI_EXCEPTION(env, raw::_invoke_object_methodV(env, &jretval, jobj, method, args));
    va_end(args);

    return jretval;
}

jvalue invoke_static_method(JNIEnv* env, jclass jcls, Method* method, ...) {
    va_list args;

    va_start(args, method);
    jvalue jretval;
    THROW_JNI_EXCEPTION(env, raw::_invoke_static_methodV(env, &jretval, jcls, method, args));
    va_end(args);

    return jretval;
}

jobject invoke_new_object(JNIEnv* env, jclass jcls, Method* method, ...) {
    va_list args;

    va_start(args, method);
    jobject jobj;
    THROW_JNI_EXCEPTION(env, raw::_invoke_new_objectV(env, &jobj, jcls, method, args));
    va_end(args);

    return jobj;
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
    if (signature[index++] != '(') {
        return "Invalid signature";
    }

    std::string buffer;

    std::string parameters;
    while (signature[index] != ')') {
        if (!parameters.empty()) {
            parameters += ", ";
        }
        parameters += parse_type(signature, index);
    }
    // skip the ')'
    ++index;

    buffer.append(parse_type(signature, index)).append(" ").append(name).append("(").append(parameters).append(")");

    return buffer;
}

std::string jstr_to_str(JNIEnv* env, jstring jstr) {
    if (!jstr) return "";
    const char* chars = env->GetStringUTFChars(jstr, nullptr);
    std::string res = chars;
    env->ReleaseStringUTFChars(jstr, chars);
    return res;
}

std::string jbytes_to_str(JNIEnv* env, jbyteArray jobj) {
    jbyte* bytes = env->GetByteArrayElements(jobj, nullptr);
    jsize length = env->GetArrayLength(jobj);
    std::string result(reinterpret_cast<char*>(bytes), length);
    env->ReleaseByteArrayElements(jobj, bytes, JNI_ABORT);
    return result;
}

jobject new_jbytes(JNIEnv* env, const char* data, size_t size) {
    jbyteArray jbytes = env->NewByteArray(size);
    env->SetByteArrayRegion(jbytes, 0, size, reinterpret_cast<const jbyte*>(data));
    return jbytes;
}

jobject get_from_jmap(JNIEnv* env, jobject jmap, const std::string& key) {
    Method m_get = get_method(env, jcls_hashmap, "get", "(Ljava/lang/Object;)Ljava/lang/Object;", false);

    AutoLocalJobject jstr_key = env->NewStringUTF(key.c_str());
    jvalue jretval = invoke_object_method(env, jmap, &m_get, jstr_key.get());
    return jretval.l;
}

jobject map_to_jmap(JNIEnv* env, const std::map<std::string, std::string>& params) {
    Method m_ctor = get_method(env, jcls_hashmap, "<init>", "()V", false);
    Method m_put =
            get_method(env, jcls_hashmap, "put", "(Ljava/lang/Object;Ljava/lang/Object;)Ljava/lang/Object;", false);

    jobject jmap = invoke_new_object(env, jcls_hashmap, &m_ctor);
    for (const auto& entry : params) {
        AutoLocalJobject jstr_key = env->NewStringUTF(entry.first.c_str());
        AutoLocalJobject jstr_value = env->NewStringUTF(entry.second.c_str());
        jvalue jretval = invoke_object_method(env, jmap, &m_put, jstr_key.get(), jstr_value.get());
        AutoLocalJobject tmp = jretval.l;
    }
    return jmap;
}

jobject vstrs_to_jlstrs(JNIEnv* env, const std::vector<std::string>& vec) {
    Method m_ctor = get_method(env, jcls_arraylist, "<init>", "()V", false);
    Method m_add = get_method(env, jcls_arraylist, "add", "(Ljava/lang/Object;)Z", false);

    jobject jlist = invoke_new_object(env, jcls_arraylist, &m_ctor);

    for (const auto& item : vec) {
        AutoLocalJobject jstr_item = env->NewStringUTF(item.c_str());
        invoke_object_method(env, jlist, &m_add, jstr_item.get());
    }
    return jlist;
}
// --------------------------- MemoryMonitor ---------------------------

MemoryMonitor::MemoryMonitor() {
    JNIEnv* env = get_env();

    {
        jcls_management_factory = find_class(env, "java/lang/management/ManagementFactory");

        m_get_memory_mxbean = get_method(env, jcls_management_factory, "getMemoryMXBean",
                                         "()Ljava/lang/management/MemoryMXBean;", true);

        jvalue return_val = invoke_static_method(env, jcls_management_factory, &m_get_memory_mxbean);
        AutoLocalJobject jmxbean = return_val.l;
        jobj_memory_mxbean = env->NewGlobalRef(jmxbean.get());
    }

    {
        jcls_memory_mxbean = find_class(env, "java/lang/management/MemoryMXBean");

        m_get_heap_memory_usage = get_method(env, jcls_memory_mxbean, "getHeapMemoryUsage",
                                             "()Ljava/lang/management/MemoryUsage;", false);
        m_get_non_heap_memory_usage = get_method(env, jcls_memory_mxbean, "getNonHeapMemoryUsage",
                                                 "()Ljava/lang/management/MemoryUsage;", false);
    }

    {
        jcls_memory_usage = find_class(env, "java/lang/management/MemoryUsage");

        m_get_init = get_method(env, jcls_memory_usage, "getInit", "()J", false);
        m_get_used = get_method(env, jcls_memory_usage, "getUsed", "()J", false);
        m_get_committed = get_method(env, jcls_memory_usage, "getCommitted", "()J", false);
        m_get_max = get_method(env, jcls_memory_usage, "getMax", "()J", false);
    }
}

MemoryMonitor& MemoryMonitor::instance() {
    static MemoryMonitor inst;
    return inst;
}

MemoryMonitor::MemoryUsage MemoryMonitor::to_memory_usage(JNIEnv* env, jobject jobj_memory_usage) {
    MemoryMonitor::MemoryUsage usage{};

    usage.init = invoke_object_method(env, jobj_memory_usage, &m_get_init).j;
    usage.used = invoke_object_method(env, jobj_memory_usage, &m_get_used).j;
    usage.committed = invoke_object_method(env, jobj_memory_usage, &m_get_committed).j;
    usage.max = invoke_object_method(env, jobj_memory_usage, &m_get_max).j;

    return usage;
}

MemoryMonitor::MemoryUsage MemoryMonitor::get_heap_memory_usage() {
    JNIEnv* env = get_env();

    AutoLocalJobject jobj_memory_usage = invoke_object_method(env, jobj_memory_mxbean, &m_get_heap_memory_usage).l;
    return to_memory_usage(env, jobj_memory_usage.get());
}

MemoryMonitor::MemoryUsage MemoryMonitor::get_nonheap_memory_usage() {
    JNIEnv* env = get_env();

    AutoLocalJobject jobj_memory_usage = invoke_object_method(env, jobj_memory_mxbean, &m_get_non_heap_memory_usage).l;
    return to_memory_usage(env, jobj_memory_usage.get());
}

} // namespace jni_utils
