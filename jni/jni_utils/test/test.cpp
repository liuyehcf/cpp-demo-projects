#include <gtest/gtest.h>
#include <jni_utils.h>

#include <iostream>
#include <thread>

template <typename... Args>
void invoke(JNIEnv* env, jobject jobj, jni_utils::Method* method, Args&&... args) {
    try {
        jvalue res = jni_utils::invoke_method(env, jobj, method, std::forward<Args>(args)...);
        if (method->is_return_ref()) {
            env->DeleteLocalRef(res.l);
        }
    } catch (const std::exception& e) {
        std::cout << e.what() << std::endl;
    }
};

TEST(JNIUtils, concurrency_safety) {
    static constexpr size_t THREAD_NUM = 64;
    static constexpr size_t _1M = 1024 * 1024;

    std::vector<std::thread> threads;
    std::atomic<size_t> count = 0;

    for (size_t i = 0; i < THREAD_NUM; ++i) {
        threads.emplace_back([&count]() {
            JNIEnv* env = jni_utils::get_env();
            jni_utils::AutoLocalJobject jbytes = env->NewByteArray(_1M);
            count += env->GetArrayLength(jbytes);
        });
    }

    for (size_t i = 0; i < THREAD_NUM; ++i) {
        threads[i].join();
    }

    ASSERT_EQ(count, THREAD_NUM * _1M);
}

TEST(JNIUtils, memory_safety) {
    static constexpr size_t TIMES = 1024;
    static constexpr size_t _1M = 1024 * 1024;

    JNIEnv* env = jni_utils::get_env();
    size_t count = 0;
    for (size_t i = 0; i < TIMES; ++i) {
        jni_utils::AutoLocalJobject jbytes = env->NewByteArray(_1M);
        count += env->GetArrayLength(jbytes);
    }

    ASSERT_EQ(count, TIMES * _1M);
}

TEST(JNIUtils, return_type) {
    JNIEnv* env = jni_utils::get_env();
    jni_utils::AutoLocalJobject jcls = jni_utils::find_class(env, "org/liuyehcf/jni/MethodReturnType");

    auto m_ctor = jni_utils::get_mid(env, jcls, "<init>", "()V", false);
    jni_utils::AutoLocalJobject jobj = env->NewObject(jcls, m_ctor.mid);

    jvalue res;
    auto m_void = jni_utils::get_mid(env, jcls, "voidMethod", "()V", false);
    res = jni_utils::invoke_method(env, jobj, &m_void);
    auto m_object = jni_utils::get_mid(env, jcls, "objectMethod", "()Ljava/lang/Object;", false);
    res = jni_utils::invoke_method(env, jobj, &m_object);
    ASSERT_EQ(nullptr, res.l);
    auto m_array = jni_utils::get_mid(env, jcls, "arrayMethod", "()[[Ljava/lang/Object;", false);
    res = jni_utils::invoke_method(env, jobj, &m_array);
    ASSERT_EQ(nullptr, res.l);
    auto m_boolean = jni_utils::get_mid(env, jcls, "booleanMethod", "()Z", false);
    res = jni_utils::invoke_method(env, jobj, &m_boolean);
    ASSERT_TRUE(res.z);
    auto m_byte = jni_utils::get_mid(env, jcls, "byteMethod", "()B", false);
    res = jni_utils::invoke_method(env, jobj, &m_byte);
    ASSERT_EQ(11, res.b);
    auto m_char = jni_utils::get_mid(env, jcls, "charMethod", "()C", false);
    res = jni_utils::invoke_method(env, jobj, &m_char);
    ASSERT_EQ(12, res.c);
    auto m_short = jni_utils::get_mid(env, jcls, "shortMethod", "()S", false);
    res = jni_utils::invoke_method(env, jobj, &m_short);
    ASSERT_EQ(13, res.s);
    auto m_int = jni_utils::get_mid(env, jcls, "intMethod", "()I", false);
    res = jni_utils::invoke_method(env, jobj, &m_int);
    ASSERT_EQ(14, res.i);
    auto m_long = jni_utils::get_mid(env, jcls, "longMethod", "()J", false);
    res = jni_utils::invoke_method(env, jobj, &m_long);
    ASSERT_EQ(15, res.j);
    auto m_float = jni_utils::get_mid(env, jcls, "floatMethod", "()F", false);
    res = jni_utils::invoke_method(env, jobj, &m_float);
    ASSERT_EQ(16, res.f);
    auto m_double = jni_utils::get_mid(env, jcls, "doubleMethod", "()D", false);
    res = jni_utils::invoke_method(env, jobj, &m_double);
    ASSERT_EQ(17, res.d);
}

TEST(JNIUtils, static_return_type) {
    JNIEnv* env = jni_utils::get_env();
    jni_utils::AutoLocalJobject jcls = jni_utils::find_class(env, "org/liuyehcf/jni/MethodReturnType");

    jvalue res;
    auto m_void = jni_utils::get_mid(env, jcls, "staticVoidMethod", "()V", true);
    jni_utils::invoke_static_method(env, jcls, &m_void);
    auto m_object = jni_utils::get_mid(env, jcls, "staticObjectMethod", "()Ljava/lang/Object;", true);
    res = jni_utils::invoke_static_method(env, jcls, &m_object);
    ASSERT_EQ(nullptr, res.l);
    auto m_array = jni_utils::get_mid(env, jcls, "staticArrayMethod", "()[I", true);
    res = jni_utils::invoke_static_method(env, jcls, &m_array);
    ASSERT_EQ(nullptr, res.l);
    auto m_boolean = jni_utils::get_mid(env, jcls, "staticBooleanMethod", "()Z", true);
    res = jni_utils::invoke_static_method(env, jcls, &m_boolean);
    ASSERT_TRUE(res.z);
    auto m_byte = jni_utils::get_mid(env, jcls, "staticByteMethod", "()B", true);
    res = jni_utils::invoke_static_method(env, jcls, &m_byte);
    ASSERT_EQ(1, res.b);
    auto m_char = jni_utils::get_mid(env, jcls, "staticCharMethod", "()C", true);
    res = jni_utils::invoke_static_method(env, jcls, &m_char);
    ASSERT_EQ(2, res.c);
    auto m_short = jni_utils::get_mid(env, jcls, "staticShortMethod", "()S", true);
    res = jni_utils::invoke_static_method(env, jcls, &m_short);
    ASSERT_EQ(3, res.s);
    auto m_int = jni_utils::get_mid(env, jcls, "staticIntMethod", "()I", true);
    res = jni_utils::invoke_static_method(env, jcls, &m_int);
    ASSERT_EQ(4, res.i);
    auto m_long = jni_utils::get_mid(env, jcls, "staticLongMethod", "()J", true);
    res = jni_utils::invoke_static_method(env, jcls, &m_long);
    ASSERT_EQ(5, res.j);
    auto m_float = jni_utils::get_mid(env, jcls, "staticFloatMethod", "()F", true);
    res = jni_utils::invoke_static_method(env, jcls, &m_float);
    ASSERT_EQ(6, res.f);
    auto m_double = jni_utils::get_mid(env, jcls, "staticDoubleMethod", "()D", true);
    res = jni_utils::invoke_static_method(env, jcls, &m_double);
    ASSERT_EQ(7, res.d);
}

TEST(JNIUtils, exception) {
    JNIEnv* env = jni_utils::get_env();
    jni_utils::AutoLocalJobject jcls = jni_utils::find_class(env, "org/liuyehcf/jni/ThrowException");

    auto m_ctor = jni_utils::get_mid(env, jcls, "<init>", "()V", false);
    jni_utils::AutoLocalJobject jobj = env->NewObject(jcls, m_ctor.mid);

    auto m_run1 = jni_utils::get_mid(env, jcls, "run1", "(Ljava/lang/Object;)V", false);
    invoke(env, jobj, &m_run1, nullptr);

    auto m_run2 = jni_utils::get_mid(env, jcls, "run2", "([Ljava/lang/Object;I)I", false);
    invoke(env, jobj, &m_run2, nullptr, 1);

    auto m_run3 = jni_utils::get_mid(env, jcls, "run3", "(D[Ljava/lang/Object;[Z)Ljava/lang/Object;", false);
    invoke(env, jobj, &m_run3, 1.0, nullptr, nullptr);

    auto m_run4 = jni_utils::get_mid(env, jcls, "run4", "([[IJLjava/lang/Object;[S)[[B", false);
    invoke(env, jobj, &m_run4, nullptr, 1L, nullptr, nullptr);

    auto m_run5 =
            jni_utils::get_mid(env, jcls, "run5", "(ZBCSIJFD[Z[[B[[[CLjava/lang/Object;[Ljava/lang/Object;)V", false);
    invoke(env, jobj, &m_run5, true, (jbyte)1, (jchar)2, (jshort)3, 4, 5, 7.0f, 8.0, nullptr, nullptr, nullptr, nullptr,
           nullptr);
}

TEST(JNIUtils, jstring_to_str) {
    JNIEnv* env = jni_utils::get_env();
    jni_utils::AutoLocalJobject jcls = jni_utils::find_class(env, "org/liuyehcf/jni/UtilMethods");

    auto m_get_string = jni_utils::get_mid(env, jcls, "getString", "()Ljava/lang/String;", true);
    jvalue return_val = jni_utils::invoke_static_method(env, jcls, &m_get_string);
    jni_utils::AutoLocalJobject jstr = return_val.l;

    std::string str = jni_utils::jstr_to_str(env, jstr);
    ASSERT_EQ(str, "Hello, JNI!");
}

TEST(JNIUtils, jbytes_to_str) {
    JNIEnv* env = jni_utils::get_env();
    jni_utils::AutoLocalJobject jcls = jni_utils::find_class(env, "org/liuyehcf/jni/UtilMethods");

    auto m_get_bytes = jni_utils::get_mid(env, jcls, "getBytes", "()[B", true);
    jvalue return_val = jni_utils::invoke_static_method(env, jcls, &m_get_bytes);
    jni_utils::AutoLocalJobject jbytes = return_val.l;

    std::string str = jni_utils::jbytes_to_str(env, jbytes);
    ASSERT_EQ(str, "Hello, JNI!");
}

TEST(JNIUtils, new_jbytes) {
    JNIEnv* env = jni_utils::get_env();
    jni_utils::AutoLocalJobject jcls = jni_utils::find_class(env, "org/liuyehcf/jni/UtilMethods");

    auto m_print = jni_utils::get_mid(env, jcls, "print", "(Ljava/lang/Object;)V", true);
    std::string str = "Hello, JNI!";
    jni_utils::AutoLocalJobject jbytes = jni_utils::new_jbytes(env, str.data(), str.size());
    jni_utils::invoke_static_method(env, jcls, &m_print, jbytes.get());
}

TEST(JNIUtils, get_from_jmap) {
    JNIEnv* env = jni_utils::get_env();
    jni_utils::AutoLocalJobject jcls = jni_utils::find_class(env, "org/liuyehcf/jni/UtilMethods");

    auto m_get_hash_map = jni_utils::get_mid(env, jcls, "getHashMap", "()Ljava/util/HashMap;", true);
    jvalue return_val = jni_utils::invoke_static_method(env, jcls, &m_get_hash_map);
    jni_utils::AutoLocalJobject jmap = return_val.l;

    std::string key1 = "key1";
    std::string key2 = "key2";
    jni_utils::AutoLocalJobject jvalue1 = jni_utils::get_from_jmap(env, jmap, key1);
    jni_utils::AutoLocalJobject jvalue2 = jni_utils::get_from_jmap(env, jmap, key2);

    std::string value1 = jni_utils::jstr_to_str(env, static_cast<jstring>(jvalue1.get()));
    std::string value2 = jni_utils::jstr_to_str(env, static_cast<jstring>(jvalue2.get()));

    ASSERT_EQ(value1, "value1");
    ASSERT_EQ(value2, "value2");
}

TEST(JNIUtils, map_to_jmap) {
    JNIEnv* env = jni_utils::get_env();
    jni_utils::AutoLocalJobject jcls = jni_utils::find_class(env, "org/liuyehcf/jni/UtilMethods");

    auto m_print = jni_utils::get_mid(env, jcls, "print", "(Ljava/lang/Object;)V", true);

    std::map<std::string, std::string> map;
    map["key1"] = "value1";
    map["key2"] = "value2";

    jni_utils::AutoLocalJobject jmap = jni_utils::map_to_jmap(env, map);
    jni_utils::invoke_static_method(env, jcls, &m_print, jmap.get());
}

TEST(JNIUtils, vstrs_to_jlstrs) {
    JNIEnv* env = jni_utils::get_env();
    jni_utils::AutoLocalJobject jcls = jni_utils::find_class(env, "org/liuyehcf/jni/UtilMethods");

    auto m_print = jni_utils::get_mid(env, jcls, "print", "(Ljava/lang/Object;)V", true);

    std::vector<std::string> vec = {"Hello", "JNI", "World"};
    jni_utils::AutoLocalJobject jlist = jni_utils::vstrs_to_jlstrs(env, vec);

    jni_utils::invoke_static_method(env, jcls, &m_print, jlist.get());
}
