cmake_minimum_required(VERSION 3.20)

project(jni_best_practice)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED True)

set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -O3 -Wall")

set(JNI_UTILS_LIB "jni_utils")
file(GLOB HEADERS "${CMAKE_SOURCE_DIR}/src/*.h")
file(GLOB SOURCES "${CMAKE_SOURCE_DIR}/src/*.cpp")
add_library(${JNI_UTILS_LIB} ${HEADERS} ${SOURCES})
target_include_directories(${JNI_UTILS_LIB} PUBLIC ${CMAKE_SOURCE_DIR}/src)

target_compile_options(${JNI_UTILS_LIB} PRIVATE -static-libstdc++)
target_link_options(${JNI_UTILS_LIB} PRIVATE -static-libstdc++)

set(JAVA_HOME $ENV{JAVA_HOME})
if("${JAVA_HOME}" STREQUAL "")
    message(FATAL_ERROR "env 'JAVA_HOME' is required")
endif()
# For high jdk version
file(GLOB LIB_JVM ${JAVA_HOME}/lib/server/libjvm.so)
if("${LIB_JVM}" STREQUAL "")
    # For low jdk version
    file(GLOB_RECURSE LIB_JVM ${JAVA_HOME}/jre/lib/*/server/libjvm.so)
    if("${LIB_JVM}" STREQUAL "")
    message(FATAL_ERROR "cannot find libjvm.so in ${JAVA_HOME}")
    endif()
endif()
add_library(jvm SHARED IMPORTED)
set_target_properties(jvm PROPERTIES IMPORTED_LOCATION ${LIB_JVM})
target_include_directories(jvm INTERFACE ${JAVA_HOME}/include)
target_include_directories(jvm INTERFACE ${JAVA_HOME}/include/linux)

target_link_libraries(${JNI_UTILS_LIB} PUBLIC jvm)

find_package(GTest REQUIRED)
file(GLOB TEST_SOURCES "${CMAKE_SOURCE_DIR}/test/*.cpp")
set(TEST_NAME "${JNI_UTILS_LIB}_test")
add_executable(${TEST_NAME} ${TEST_SOURCES})
target_link_libraries(${TEST_NAME} PRIVATE ${JNI_UTILS_LIB})
target_link_libraries(${TEST_NAME} PRIVATE ${GTEST_LIBRARIES})
target_link_libraries(${TEST_NAME} PRIVATE ${GTEST_MAIN_LIBRARIES})

file(GLOB_RECURSE TEST_JAVA_SOURCES "${CMAKE_SOURCE_DIR}/test/java/*.java")
add_custom_target(build_java
    COMMAND javac -d ${CMAKE_BINARY_DIR}/classes ${TEST_JAVA_SOURCES}
    DEPENDS ${TEST_JAVA_SOURCES}
    WORKING_DIRECTORY ${CMAKE_SOURCE_DIR} ${CMAKE_SOURCE_DIR}/test/java
)
add_dependencies(${TEST_NAME} build_java)
