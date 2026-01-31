#include <gtest/gtest.h>
#include <jni_utils.h>

#include <iostream>
#include <thread>

template <typename... Args>
void invoke(JNIEnv* env, jobject jobj, jni_utils::Method* method, Args&&... args) {
    try {
        jvalue jretval = jni_utils::invoke_object_method(env, jobj, method, std::forward<Args>(args)...);
        if (method->is_return_ref()) {
            env->DeleteLocalRef(jretval.l);
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
    jni_utils::AutoGlobalJobject jcls = jni_utils::find_class(env, "org/liuyehcf/jni/MethodReturnType");

    auto m_ctor = jni_utils::get_method(env, jcls, "<init>", "()V", false);
    jni_utils::AutoLocalJobject jobj = env->NewObject(jcls, m_ctor.jmid);

    jvalue jretval;
    auto m_void = jni_utils::get_method(env, jcls, "voidMethod", "()V", false);
    jretval = jni_utils::invoke_object_method(env, jobj, &m_void);
    auto m_object = jni_utils::get_method(env, jcls, "objectMethod", "()Ljava/lang/Object;", false);
    jretval = jni_utils::invoke_object_method(env, jobj, &m_object);
    ASSERT_EQ(nullptr, jretval.l);
    auto m_array = jni_utils::get_method(env, jcls, "arrayMethod", "()[[Ljava/lang/Object;", false);
    jretval = jni_utils::invoke_object_method(env, jobj, &m_array);
    ASSERT_EQ(nullptr, jretval.l);
    auto m_boolean = jni_utils::get_method(env, jcls, "booleanMethod", "()Z", false);
    jretval = jni_utils::invoke_object_method(env, jobj, &m_boolean);
    ASSERT_TRUE(jretval.z);
    auto m_byte = jni_utils::get_method(env, jcls, "byteMethod", "()B", false);
    jretval = jni_utils::invoke_object_method(env, jobj, &m_byte);
    ASSERT_EQ(11, jretval.b);
    auto m_char = jni_utils::get_method(env, jcls, "charMethod", "()C", false);
    jretval = jni_utils::invoke_object_method(env, jobj, &m_char);
    ASSERT_EQ(12, jretval.c);
    auto m_short = jni_utils::get_method(env, jcls, "shortMethod", "()S", false);
    jretval = jni_utils::invoke_object_method(env, jobj, &m_short);
    ASSERT_EQ(13, jretval.s);
    auto m_int = jni_utils::get_method(env, jcls, "intMethod", "()I", false);
    jretval = jni_utils::invoke_object_method(env, jobj, &m_int);
    ASSERT_EQ(14, jretval.i);
    auto m_long = jni_utils::get_method(env, jcls, "longMethod", "()J", false);
    jretval = jni_utils::invoke_object_method(env, jobj, &m_long);
    ASSERT_EQ(15, jretval.j);
    auto m_float = jni_utils::get_method(env, jcls, "floatMethod", "()F", false);
    jretval = jni_utils::invoke_object_method(env, jobj, &m_float);
    ASSERT_EQ(16, jretval.f);
    auto m_double = jni_utils::get_method(env, jcls, "doubleMethod", "()D", false);
    jretval = jni_utils::invoke_object_method(env, jobj, &m_double);
    ASSERT_EQ(17, jretval.d);
}

TEST(JNIUtils, static_return_type) {
    JNIEnv* env = jni_utils::get_env();
    jni_utils::AutoGlobalJobject jcls = jni_utils::find_class(env, "org/liuyehcf/jni/MethodReturnType");

    jvalue jretval;
    auto m_void = jni_utils::get_method(env, jcls, "staticVoidMethod", "()V", true);
    jni_utils::invoke_static_method(env, jcls, &m_void);
    auto m_object = jni_utils::get_method(env, jcls, "staticObjectMethod", "()Ljava/lang/Object;", true);
    jretval = jni_utils::invoke_static_method(env, jcls, &m_object);
    ASSERT_EQ(nullptr, jretval.l);
    auto m_array = jni_utils::get_method(env, jcls, "staticArrayMethod", "()[I", true);
    jretval = jni_utils::invoke_static_method(env, jcls, &m_array);
    ASSERT_EQ(nullptr, jretval.l);
    auto m_boolean = jni_utils::get_method(env, jcls, "staticBooleanMethod", "()Z", true);
    jretval = jni_utils::invoke_static_method(env, jcls, &m_boolean);
    ASSERT_TRUE(jretval.z);
    auto m_byte = jni_utils::get_method(env, jcls, "staticByteMethod", "()B", true);
    jretval = jni_utils::invoke_static_method(env, jcls, &m_byte);
    ASSERT_EQ(1, jretval.b);
    auto m_char = jni_utils::get_method(env, jcls, "staticCharMethod", "()C", true);
    jretval = jni_utils::invoke_static_method(env, jcls, &m_char);
    ASSERT_EQ(2, jretval.c);
    auto m_short = jni_utils::get_method(env, jcls, "staticShortMethod", "()S", true);
    jretval = jni_utils::invoke_static_method(env, jcls, &m_short);
    ASSERT_EQ(3, jretval.s);
    auto m_int = jni_utils::get_method(env, jcls, "staticIntMethod", "()I", true);
    jretval = jni_utils::invoke_static_method(env, jcls, &m_int);
    ASSERT_EQ(4, jretval.i);
    auto m_long = jni_utils::get_method(env, jcls, "staticLongMethod", "()J", true);
    jretval = jni_utils::invoke_static_method(env, jcls, &m_long);
    ASSERT_EQ(5, jretval.j);
    auto m_float = jni_utils::get_method(env, jcls, "staticFloatMethod", "()F", true);
    jretval = jni_utils::invoke_static_method(env, jcls, &m_float);
    ASSERT_EQ(6, jretval.f);
    auto m_double = jni_utils::get_method(env, jcls, "staticDoubleMethod", "()D", true);
    jretval = jni_utils::invoke_static_method(env, jcls, &m_double);
    ASSERT_EQ(7, jretval.d);
}

TEST(JNIUtils, exception) {
    JNIEnv* env = jni_utils::get_env();
    jni_utils::AutoGlobalJobject jcls = jni_utils::find_class(env, "org/liuyehcf/jni/ThrowException");

    auto m_ctor = jni_utils::get_method(env, jcls, "<init>", "()V", false);
    jni_utils::AutoLocalJobject jobj = env->NewObject(jcls, m_ctor.jmid);

    auto m_run1 = jni_utils::get_method(env, jcls, "run1", "(Ljava/lang/Object;)V", false);
    invoke(env, jobj, &m_run1, nullptr);

    auto m_run2 = jni_utils::get_method(env, jcls, "run2", "([Ljava/lang/Object;I)I", false);
    invoke(env, jobj, &m_run2, nullptr, 1);

    auto m_run3 = jni_utils::get_method(env, jcls, "run3", "(D[Ljava/lang/Object;[Z)Ljava/lang/Object;", false);
    invoke(env, jobj, &m_run3, 1.0, nullptr, nullptr);

    auto m_run4 = jni_utils::get_method(env, jcls, "run4", "([[IJLjava/lang/Object;[S)[[B", false);
    invoke(env, jobj, &m_run4, nullptr, 1L, nullptr, nullptr);

    auto m_run5 = jni_utils::get_method(env, jcls, "run5", "(ZBCSIJFD[Z[[B[[[CLjava/lang/Object;[Ljava/lang/Object;)V",
                                        false);
    invoke(env, jobj, &m_run5, true, (jbyte)1, (jchar)2, (jshort)3, 4, 5, 7.0f, 8.0, nullptr, nullptr, nullptr, nullptr,
           nullptr);
}

TEST(JNIUtils, jstring_to_str) {
    JNIEnv* env = jni_utils::get_env();
    jni_utils::AutoGlobalJobject jcls = jni_utils::find_class(env, "org/liuyehcf/jni/UtilMethods");

    auto m_get_string = jni_utils::get_method(env, jcls, "getString", "()Ljava/lang/String;", true);
    jvalue jretval = jni_utils::invoke_static_method(env, jcls, &m_get_string);
    jni_utils::AutoLocalJobject jstr = jretval.l;

    std::string str = jni_utils::jstr_to_str(env, jstr);
    ASSERT_EQ(str, "Hello, JNI!");
}

TEST(JNIUtils, jbytes_to_str) {
    JNIEnv* env = jni_utils::get_env();
    jni_utils::AutoGlobalJobject jcls = jni_utils::find_class(env, "org/liuyehcf/jni/UtilMethods");

    auto m_get_bytes = jni_utils::get_method(env, jcls, "getBytes", "()[B", true);
    jvalue jretval = jni_utils::invoke_static_method(env, jcls, &m_get_bytes);
    jni_utils::AutoLocalJobject jbytes = jretval.l;

    std::string str = jni_utils::jbytes_to_str(env, jbytes);
    ASSERT_EQ(str, "Hello, JNI!");
}

TEST(JNIUtils, new_jbytes) {
    JNIEnv* env = jni_utils::get_env();
    jni_utils::AutoGlobalJobject jcls = jni_utils::find_class(env, "org/liuyehcf/jni/UtilMethods");

    auto m_print = jni_utils::get_method(env, jcls, "print", "(Ljava/lang/Object;)V", true);
    std::string str = "Hello, JNI!";
    jni_utils::AutoLocalJobject jbytes = jni_utils::new_jbytes(env, str.data(), str.size());
    jni_utils::invoke_static_method(env, jcls, &m_print, jbytes.get());
}

TEST(JNIUtils, get_from_jmap) {
    JNIEnv* env = jni_utils::get_env();
    jni_utils::AutoGlobalJobject jcls = jni_utils::find_class(env, "org/liuyehcf/jni/UtilMethods");

    auto m_get_hash_map = jni_utils::get_method(env, jcls, "getHashMap", "()Ljava/util/HashMap;", true);
    jvalue jretval = jni_utils::invoke_static_method(env, jcls, &m_get_hash_map);
    jni_utils::AutoLocalJobject jmap = jretval.l;

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
    jni_utils::AutoGlobalJobject jcls = jni_utils::find_class(env, "org/liuyehcf/jni/UtilMethods");

    auto m_print = jni_utils::get_method(env, jcls, "print", "(Ljava/lang/Object;)V", true);

    std::map<std::string, std::string> map;
    map["key1"] = "value1";
    map["key2"] = "value2";

    jni_utils::AutoLocalJobject jmap = jni_utils::map_to_jmap(env, map);
    jni_utils::invoke_static_method(env, jcls, &m_print, jmap.get());
}

TEST(JNIUtils, vstrs_to_jlstrs) {
    JNIEnv* env = jni_utils::get_env();
    jni_utils::AutoLocalJobject jcls = jni_utils::find_class(env, "org/liuyehcf/jni/UtilMethods");

    auto m_print = jni_utils::get_method(env, jcls, "print", "(Ljava/lang/Object;)V", true);

    std::vector<std::string> vec = {"Hello", "JNI", "World"};
    jni_utils::AutoLocalJobject jlist = jni_utils::vstrs_to_jlstrs(env, vec);

    jni_utils::invoke_static_method(env, jcls, &m_print, jlist.get());
}

TEST(JNIUtils, memory_monitor_basic) {
    auto& mm = jni_utils::MemoryMonitor::instance();

    auto heap = mm.get_heap_memory_usage();
    auto nonheap = mm.get_nonheap_memory_usage();

    std::cout << "Heap: init=" << heap.init << " used=" << heap.used << " committed=" << heap.committed
              << " max=" << heap.max << std::endl;
    std::cout << "NonHeap: init=" << nonheap.init << " used=" << nonheap.used << " committed=" << nonheap.committed
              << " max=" << nonheap.max << std::endl;

    // Heap assertions
    ASSERT_GE(heap.used, 0);
    ASSERT_GE(heap.committed, 0);
    ASSERT_LE(heap.used, heap.committed);
    if (heap.max != -1) {
        ASSERT_LE(heap.committed, heap.max);
    }

    // Non-heap assertions
    ASSERT_GE(nonheap.used, 0);
    ASSERT_GE(nonheap.committed, 0);
    ASSERT_LE(nonheap.used, nonheap.committed);
    if (nonheap.max != -1) {
        ASSERT_LE(nonheap.committed, nonheap.max);
    }
}
