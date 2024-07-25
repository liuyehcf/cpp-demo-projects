cmake_minimum_required(VERSION 3.20)

project(jni_utils)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED True)

set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -O3 -Wall")

file(GLOB HEADERS "${CMAKE_CURRENT_SOURCE_DIR}/src/*.h")
file(GLOB SOURCES "${CMAKE_CURRENT_SOURCE_DIR}/src/*.cpp")
add_library(${PROJECT_NAME} ${HEADERS} ${SOURCES})
target_include_directories(${PROJECT_NAME} PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/src)

target_compile_options(${PROJECT_NAME} PRIVATE -static-libstdc++)
target_link_options(${PROJECT_NAME} PRIVATE -static-libstdc++)

set(JAVA_HOME $ENV{JAVA_HOME})
if("${JAVA_HOME}" STREQUAL "")
    message(FATAL_ERROR "env 'JAVA_HOME' is required")
endif()
# For high jdk version
string(TOLOWER "${CMAKE_SYSTEM_NAME}" SYSTEM_NAME_LOWER)
if(${CMAKE_SYSTEM_NAME} STREQUAL "Linux")
    set(DYLIB_SUFFIX "so")
elseif(${CMAKE_SYSTEM_NAME} STREQUAL "Darwin")
    set(DYLIB_SUFFIX "dylib")
endif()
file(GLOB LIB_JVM ${JAVA_HOME}/lib/server/libjvm.${DYLIB_SUFFIX})
if("${LIB_JVM}" STREQUAL "")
    # For low jdk version
    file(GLOB_RECURSE LIB_JVM_1 ${JAVA_HOME}/jre/lib/server/libjvm.${DYLIB_SUFFIX})
    file(GLOB_RECURSE LIB_JVM_2 ${JAVA_HOME}/jre/lib/*/server/libjvm.${DYLIB_SUFFIX})
    set(LIB_JVM ${LIB_JVM_1} ${LIB_JVM_2})
    if("${LIB_JVM}" STREQUAL "")
        message(FATAL_ERROR "cannot find libjvm.${DYLIB_SUFFIX} in ${JAVA_HOME}")
    endif()
endif()
add_library(jvm SHARED IMPORTED)
set_target_properties(jvm PROPERTIES IMPORTED_LOCATION ${LIB_JVM})
target_include_directories(jvm INTERFACE ${JAVA_HOME}/include)
target_include_directories(jvm INTERFACE ${JAVA_HOME}/include/${SYSTEM_NAME_LOWER})

target_link_libraries(${PROJECT_NAME} PUBLIC jvm)

option(ENABLE_JNI_UTIL_TEST "Whether enable tests" OFF)
if(ENABLE_JNI_UTIL_TEST)
    find_package(GTest REQUIRED)
    file(GLOB TEST_SOURCES "${CMAKE_CURRENT_SOURCE_DIR}/test/*.cpp")
    set(TEST_NAME "${PROJECT_NAME}_test")
    add_executable(${TEST_NAME} ${TEST_SOURCES})
    target_link_libraries(${TEST_NAME} PRIVATE ${PROJECT_NAME})
    target_link_libraries(${TEST_NAME} PRIVATE ${GTEST_LIBRARIES})
    target_link_libraries(${TEST_NAME} PRIVATE ${GTEST_MAIN_LIBRARIES})

    file(GLOB_RECURSE TEST_JAVA_SOURCES "${CMAKE_CURRENT_SOURCE_DIR}/test/java/*.java")
    add_custom_target(build_java
        COMMAND ${CMAKE_COMMAND} -E make_directory ${CMAKE_CURRENT_BINARY_DIR}/classes
        COMMAND javac -d ${CMAKE_CURRENT_BINARY_DIR}/classes ${TEST_JAVA_SOURCES}
        DEPENDS ${TEST_JAVA_SOURCES}
        WORKING_DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR}/test/java
    )
    add_dependencies(${TEST_NAME} build_java)
endif()
