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
        jcls_class = static_cast<jclass>(env->NewGlobalRef(jcls));
        env->DeleteLocalRef(jcls);
    }
    {
        jclass jcls = find_class(env, "java/util/List");
        jcls_list = static_cast<jclass>(env->NewGlobalRef(jcls));
        env->DeleteLocalRef(jcls);
    }
    {
        jclass jcls = find_class(env, "java/util/ArrayList");
        jcls_arraylist = static_cast<jclass>(env->NewGlobalRef(jcls));
        env->DeleteLocalRef(jcls);
    }
    {
        jclass jcls = find_class(env, "java/util/HashMap");
        jcls_hashmap = static_cast<jclass>(env->NewGlobalRef(jcls));
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

jclass find_class(JNIEnv* env, const char* classname) {
    jclass jcls = nullptr;
    CHECK_JNI_EXCEPTION(raw::_find_class(env, &jcls, classname), "Cannot find class " + std::string(classname));
    return jcls;
}

Method get_method(JNIEnv* env, jclass jcls, const char* name, const char* sig, bool is_static) {
    jmethodID jmid = nullptr;
    CHECK_JNI_EXCEPTION(raw::_find_method_id(env, &jmid, jcls, name, sig, is_static),
                        "Cannot find method " + std::string(name));
    return {jmid, name, sig};
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

std::string jbytes_to_str(JNIEnv* env, jbyteArray obj) {
    jbyte* bytes = env->GetByteArrayElements(obj, nullptr);
    jsize length = env->GetArrayLength(obj);
    std::string result(reinterpret_cast<char*>(bytes), length);
    env->ReleaseByteArrayElements(obj, bytes, JNI_ABORT);
    return result;
}

jobject new_jbytes(JNIEnv* env, const char* data, size_t size) {
    jbyteArray jbytes = env->NewByteArray(size);
    env->SetByteArrayRegion(jbytes, 0, size, reinterpret_cast<const jbyte*>(data));
    return jbytes;
}

jobject get_from_jmap(JNIEnv* env, jobject jmap, const std::string& key) {
    const char* signature_get = "(Ljava/lang/Object;)Ljava/lang/Object;";
    Method m_get = get_method(env, jcls_hashmap, "get", signature_get, false);

    AutoLocalJobject jstr_key = env->NewStringUTF(key.c_str());
    jvalue jretval = invoke_object_method(env, jmap, &m_get, jstr_key.get());
    return jretval.l;
}

jobject map_to_jmap(JNIEnv* env, const std::map<std::string, std::string>& params) {
    const char* signature_ctor = "()V";
    const char* signature_put = "(Ljava/lang/Object;Ljava/lang/Object;)Ljava/lang/Object;";
    Method m_ctor = get_method(env, jcls_hashmap, "<init>", signature_ctor, false);
    Method m_put = get_method(env, jcls_hashmap, "put", signature_put, false);

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
    const char* signature_ctor = "()V";
    const char* signature_add = "(Ljava/lang/Object;)Z";
    Method m_ctor = get_method(env, jcls_arraylist, "<init>", signature_ctor, false);
    Method m_add = get_method(env, jcls_arraylist, "add", signature_add, false);

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
    AutoLocalJobject jcls_mf = find_class(env, "java/lang/management/ManagementFactory");
    Method m_get_mxbean = get_method(env, jcls_mf, "getMemoryMXBean", "()Ljava/lang/management/MemoryMXBean;", true);
    AutoLocalJobject jmxbean = invoke_static_method(env, jcls_mf, &m_get_mxbean).l;
    _mxbean = env->NewGlobalRef(jmxbean.get());
}

MemoryMonitor& MemoryMonitor::instance() {
    static MemoryMonitor inst;
    return inst;
}

static MemoryMonitor::MemoryUsage to_memory_usage(JNIEnv* env, jobject jusage) {
    MemoryMonitor::MemoryUsage usage{};
    AutoLocalJobject jcls_usage = find_class(env, "java/lang/management/MemoryUsage");
    Method m_get_init = get_method(env, jcls_usage, "getInit", "()J", false);
    Method m_get_used = get_method(env, jcls_usage, "getUsed", "()J", false);
    Method m_get_committed = get_method(env, jcls_usage, "getCommitted", "()J", false);
    Method m_get_max = get_method(env, jcls_usage, "getMax", "()J", false);

    usage.init = invoke_object_method(env, jusage, &m_get_init).j;
    usage.used = invoke_object_method(env, jusage, &m_get_used).j;
    usage.committed = invoke_object_method(env, jusage, &m_get_committed).j;
    usage.max = invoke_object_method(env, jusage, &m_get_max).j;
    return usage;
}

MemoryMonitor::MemoryUsage MemoryMonitor::get_heap_memory_usage() {
    JNIEnv* env = get_env();
    AutoLocalJobject jcls_mx = find_class(env, "java/lang/management/MemoryMXBean");
    Method m_get_heap = get_method(env, jcls_mx, "getHeapMemoryUsage", "()Ljava/lang/management/MemoryUsage;", false);
    AutoLocalJobject jusage = invoke_object_method(env, _mxbean, &m_get_heap).l;
    return to_memory_usage(env, jusage.get());
}

MemoryMonitor::MemoryUsage MemoryMonitor::get_nonheap_memory_usage() {
    JNIEnv* env = get_env();
    AutoLocalJobject jcls_mx = find_class(env, "java/lang/management/MemoryMXBean");
    Method m_get_nonheap =
            get_method(env, jcls_mx, "getNonHeapMemoryUsage", "()Ljava/lang/management/MemoryUsage;", false);
    AutoLocalJobject jusage = invoke_object_method(env, _mxbean, &m_get_nonheap).l;
    return to_memory_usage(env, jusage.get());
}

std::unordered_map<std::string, MemoryMonitor::MemoryUsage> MemoryMonitor::get_pooled_heap_memory_usage() {
    // Fetch pools from ManagementFactory each call (avoid storing in non-movable vector)
    std::unordered_map<std::string, MemoryMonitor::MemoryUsage> result;
    JNIEnv* env = get_env();

    AutoLocalJobject jcls_mf = find_class(env, "java/lang/management/ManagementFactory");
    Method m_get_pools = get_method(env, jcls_mf, "getMemoryPoolMXBeans", "()Ljava/util/List;", true);
    AutoLocalJobject jlist = invoke_static_method(env, jcls_mf, &m_get_pools).l;

    // List APIs
    Method m_size = get_method(env, jcls_list, "size", "()I", false);
    Method m_get = get_method(env, jcls_list, "get", "(I)Ljava/lang/Object;", false);

    // MemoryPoolMXBean APIs
    AutoLocalJobject jcls_pool = find_class(env, "java/lang/management/MemoryPoolMXBean");
    Method m_pool_get_type = get_method(env, jcls_pool, "getType", "()Ljava/lang/management/MemoryType;", false);
    Method m_get_name = get_method(env, jcls_pool, "getName", "()Ljava/lang/String;", false);
    Method m_get_usage = get_method(env, jcls_pool, "getUsage", "()Ljava/lang/management/MemoryUsage;", false);

    // MemoryType APIs
    AutoLocalJobject jcls_type = find_class(env, "java/lang/management/MemoryType");
    Method m_type_to_string = get_method(env, jcls_type, "toString", "()Ljava/lang/String;", false);

    jint size = invoke_object_method(env, jlist, &m_size).i;
    for (jint i = 0; i < size; ++i) {
        AutoLocalJobject jpool = invoke_object_method(env, jlist, &m_get, i).l;
        AutoLocalJobject jtype = invoke_object_method(env, jpool, &m_pool_get_type).l;
        AutoLocalJobject jtype_str = invoke_object_method(env, jtype, &m_type_to_string).l;
        std::string type_str = jstr_to_str(env, static_cast<jstring>(jtype_str.get()));
        if (type_str == "HEAP") {
            AutoLocalJobject jname = invoke_object_method(env, jpool, &m_get_name).l;
            std::string name = jstr_to_str(env, static_cast<jstring>(jname.get()));
            AutoLocalJobject jusage = invoke_object_method(env, jpool, &m_get_usage).l;
            result.emplace(name, to_memory_usage(env, jusage.get()));
        }
    }

    return result;
}

} // namespace jni_utils
