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
