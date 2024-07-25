cmake_minimum_required(VERSION 3.20)

project(bthread_jni_issue)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED True)

add_compile_options(-O3 -Wall)

# Libs
find_package(Threads REQUIRED)
find_package(OpenSSL REQUIRED)
find_package(Gflags REQUIRED)
find_package(Glog REQUIRED)
find_package(Protobuf REQUIRED)
find_library(LEVELDB_LIB NAMES leveldb)
find_library(BRPC_LIB NAMES brpc)
find_library(THRIFT_LIB NAMES thrift)
find_package(ZLIB REQUIRED)
add_subdirectory(${CMAKE_SOURCE_DIR}/../jni_utils ${CMAKE_BINARY_DIR}/jni_utils_build)

# Set lib include dirs
set(ALL_LIB_INCLUDE_DIRS
    ${PROTOBUF_INCLUDE_DIRS}
    ${OPENSSL_INCLUDE_DIR})

message(STATUS "ALL_LIB_INCLUDE_DIRS: ${ALL_LIB_INCLUDE_DIRS}")

# Set libs (order matters)
set(ALL_LIBS
    ${BRPC_LIB}
    ${CMAKE_THREAD_LIBS_INIT}
    ${GFLAGS_LIBRARIES}
    ${PROTOBUF_LIBRARIES}
    ${LEVELDB_LIB}
    ${OPENSSL_CRYPTO_LIBRARY}
    ${OPENSSL_SSL_LIBRARY}
    ${THRIFT_LIB}
    ${ZLIB_LIBRARIES}
    jni_utils
    glog::glog
    dl)

message(STATUS "ALL_LIBS: ${ALL_LIBS}")

# Generate protobuf
file(GLOB PROTO_DEFINITION ${CMAKE_SOURCE_DIR}/src/proto/*.proto)
protobuf_generate_cpp(PROTO_SOURCE PROTO_HEADER ${PROTO_DEFINITION})

# Java
file(GLOB_RECURSE TEST_JAVA_SOURCES "${CMAKE_SOURCE_DIR}/src/java/*.java")
add_custom_target(build_java
    COMMAND ${CMAKE_COMMAND} -E make_directory ${CMAKE_BINARY_DIR}/classes
    COMMAND javac -d ${CMAKE_BINARY_DIR}/classes ${TEST_JAVA_SOURCES}
    DEPENDS ${TEST_JAVA_SOURCES}
    WORKING_DIRECTORY ${CMAKE_SOURCE_DIR}/src/java
)

# Server
file(GLOB SERVER_SOURCE ${CMAKE_SOURCE_DIR}/src/server/*.cpp)
add_executable(echo_server ${SERVER_SOURCE} ${PROTO_SOURCE})
target_include_directories(echo_server PRIVATE ${BRPC_INCLUDE_PATH} ${CMAKE_BINARY_DIR})
target_link_libraries(echo_server PRIVATE ${ALL_LIBS})
add_dependencies(echo_server build_java)

# Client
file(GLOB CLIENT_SOURCE ${CMAKE_SOURCE_DIR}/src/client/*.cpp)
add_executable(echo_client ${CLIENT_SOURCE} ${PROTO_SOURCE})
target_include_directories(echo_client PRIVATE ${ALL_LIB_INCLUDE_DIRS} ${CMAKE_BINARY_DIR})
target_link_libraries(echo_client PRIVATE ${ALL_LIBS})
