#include <jni.h>

#include <cstring>
#include <iostream>
#include <sstream>

#define ASSERT_TRUE(expr)                                                                                    \
    do {                                                                                                     \
        if (!(expr)) {                                                                                       \
            std::cerr << "(" << __FILE__ << ":" << __LINE__ << ") Assertion failed: " << #expr << std::endl; \
            if (env->ExceptionOccurred()) {                                                                  \
                env->ExceptionDescribe();                                                                    \
            }                                                                                                \
            jvm->DestroyJavaVM();                                                                            \
            exit(1);                                                                                         \
            __builtin_unreachable();                                                                         \
        }                                                                                                    \
    } while (0)

#define TOKEN_CONCAT(x, y) x##y
#define TOKEN_CONCAT_FWD(x, y) TOKEN_CONCAT(x, y)
#define DEFER(expr) [[maybe_unused]] Defer TOKEN_CONCAT_FWD(defer_, __LINE__)([&]() { expr; })
#define LOCAL_REF_GUARD(obj) DEFER(if (obj != nullptr) { env->DeleteLocalRef(obj); })

template <typename T>
class Defer {
public:
    Defer(T&& t) : t(std::forward<T>(t)) {}
    ~Defer() { t(); }

private:
    T t;
};

class ClassLoader {
public:
    virtual jclass load_class(const char* class_name) = 0;
    virtual ~ClassLoader() {}
};

class NormalClassLoader : public ClassLoader {
public:
    NormalClassLoader(JavaVM* jvm_, JNIEnv* env_) : jvm(jvm_), env(env_) {
        std::cout << "Using NormalClassLoader" << std::endl;
    }
    ~NormalClassLoader() override {}

    jclass load_class(const char* class_name) override {
        jclass cls = env->FindClass(class_name);
        ASSERT_TRUE(cls != nullptr);
        return cls;
    }

private:
    JavaVM* jvm;
    JNIEnv* env;
};

class SpringClassLoader : public ClassLoader {
public:
    SpringClassLoader(JavaVM* jvm_, JNIEnv* env_) : jvm(jvm_), env(env_) {
        std::cout << "Using SpringClassLoader" << std::endl;
        init_fat_jar_class_loader();
    }
    ~SpringClassLoader() override {
        if (jgspring_class_loader != nullptr) env->DeleteGlobalRef(jgspring_class_loader);
    }

    jclass load_class(const char* class_name) override {
        jstring jclass_name = env->NewStringUTF(class_name);
        LOCAL_REF_GUARD(jclass_name);
        jclass cls =
                static_cast<jclass>(env->CallObjectMethod(jgspring_class_loader, m_spring_load_class, jclass_name));
        ASSERT_TRUE(cls != nullptr);
        return cls;
    }

private:
    /*
     * Spring API may vary from time to time, I'm not sure if this code can work well with other spring versions
     *
     * For 2.1.4.RELEASE
     * https://github.com/spring-projects/spring-boot/blob/v2.1.4.RELEASE/spring-boot-project/spring-boot-tools/spring-boot-loader/src/main/java/org/springframework/boot/loader/Launcher.java
     *
     * protected void launch(String[] args) {
     *     try {
     *         JarFile.registerUrlProtocolHandler();
     *         ClassLoader classLoader = createClassLoader(getClassPathArchives());
     *         launch(args, getMainClass(), classLoader);
     *     }
     *     catch (Exception ex) {
     *         ex.printStackTrace();
     *         System.exit(1);
     *     }
     * }
     */
    void init_fat_jar_class_loader() {
        jclass cls_jar_launcher = env->FindClass("org/springframework/boot/loader/JarLauncher");
        ASSERT_TRUE(cls_jar_launcher != nullptr);
        LOCAL_REF_GUARD(cls_jar_launcher);

        jmethodID m_jar_launcher_init = env->GetMethodID(cls_jar_launcher, "<init>", "()V");
        ASSERT_TRUE(m_jar_launcher_init != nullptr);
        jmethodID m_get_class_path_archives =
                env->GetMethodID(cls_jar_launcher, "getClassPathArchives", "()Ljava/util/List;");
        ASSERT_TRUE(m_get_class_path_archives != nullptr);
        jmethodID m_create_class_loader =
                env->GetMethodID(cls_jar_launcher, "createClassLoader", "(Ljava/util/List;)Ljava/lang/ClassLoader;");
        ASSERT_TRUE(m_create_class_loader != nullptr);

        // Create JarLauncher instance
        jobject jar_launcher = env->NewObject(cls_jar_launcher, m_jar_launcher_init);
        ASSERT_TRUE(jar_launcher != nullptr);
        LOCAL_REF_GUARD(jar_launcher);

        // Call JarLauncher.getClassPathArchives()
        jobject jarchives = env->CallObjectMethod(jar_launcher, m_get_class_path_archives);
        ASSERT_TRUE(jarchives != nullptr);
        LOCAL_REF_GUARD(jarchives);

        // Call JarLauncher.createClassLoader(jarchives)
        jobject jspring_class_loader = env->CallObjectMethod(jar_launcher, m_create_class_loader, jarchives);
        ASSERT_TRUE(jspring_class_loader != nullptr);
        LOCAL_REF_GUARD(jspring_class_loader);

        this->jgspring_class_loader = env->NewGlobalRef(jspring_class_loader);

        jclass cls_spring_class_loader = env->GetObjectClass(jgspring_class_loader);
        ASSERT_TRUE(cls_spring_class_loader != nullptr);
        LOCAL_REF_GUARD(cls_spring_class_loader);

        this->m_spring_load_class =
                env->GetMethodID(cls_spring_class_loader, "loadClass", "(Ljava/lang/String;)Ljava/lang/Class;");
        ASSERT_TRUE(m_spring_load_class != nullptr);
    }

private:
    JavaVM* jvm;
    JNIEnv* env;
    jobject jgspring_class_loader;
    jmethodID m_spring_load_class;
};

int main(int argc, char* argv[]) {
    const std::string usage = "Usage: " + std::string(argv[0]) + " <normal|spring> <fat_jar_path> <class_name>";
    if (argc < 4 || (std::strcmp(argv[1], "normal") != 0 && std::strcmp(argv[1], "spring") != 0)) {
        std::cerr << usage << std::endl;
        return 1;
    }
    const bool normal_mode = std::strcmp(argv[1], "normal") == 0;
    const char* jar_path = argv[2];
    const std::string class_names = argv[3];

    JavaVM* jvm;
    JNIEnv* env;
    JavaVMInitArgs vm_args;
    JavaVMOption options[1];
    std::string class_path = std::string("-Djava.class.path=").append(jar_path);
    options[0].optionString = const_cast<char*>(class_path.c_str());
    vm_args.version = JNI_VERSION_1_8;
    vm_args.nOptions = 1;
    vm_args.options = options;
    vm_args.ignoreUnrecognized = false;

    jint res = JNI_CreateJavaVM(&jvm, (void**)&env, &vm_args);
    ASSERT_TRUE(res == JNI_OK);
    DEFER(jvm->DestroyJavaVM());

    ClassLoader* loader;
    if (normal_mode) {
        loader = new NormalClassLoader(jvm, env);
    } else {
        loader = new SpringClassLoader(jvm, env);
    }
    DEFER(delete loader);

    std::istringstream class_names_is(class_names);
    std::string class_name;
    while (std::getline(class_names_is, class_name, ',')) {
        jclass cls = loader->load_class(class_name.c_str());
        ASSERT_TRUE(cls != nullptr);
        std::cout << "    Find class: " << class_name << std::endl;
        LOCAL_REF_GUARD(cls);
    }

    return 0;
}
