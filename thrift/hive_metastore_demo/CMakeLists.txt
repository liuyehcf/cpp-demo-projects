cmake_minimum_required(VERSION 3.20)
project(hive_metastore_demo)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED True)

add_compile_options(-O0 -Wall -g -ggdb)

# thrift
find_library(THRIFT_LIBRARY thrift)
set(THRIFT_FILE ${CMAKE_SOURCE_DIR}/thrift/hive_metastore.thrift)
set(GEN_DIR ${CMAKE_BINARY_DIR}/gen-cpp)
set(THRIFT_SRCS
    ${GEN_DIR}/hive_metastore_types.cpp
    ${GEN_DIR}/hive_metastore_constants.cpp
    ${GEN_DIR}/ThriftHiveMetastore.cpp
)
set(THRIFT_HDRS
    ${GEN_DIR}/hive_metastore_types.h
    ${GEN_DIR}/hive_metastore_constants.h
    ${GEN_DIR}/ThriftHiveMetastore.h
)
add_custom_command(
    OUTPUT ${THRIFT_SRCS} ${THRIFT_HDRS}
    COMMAND ${CMAKE_COMMAND} -E make_directory ${GEN_DIR}
    COMMAND thrift --gen cpp -out ${GEN_DIR} ${THRIFT_FILE}
    DEPENDS ${THRIFT_FILE}
    COMMENT "Generating C++ source from thrift file"
    VERBATIM
)
add_custom_target(gen-thrift-cpp 
    DEPENDS ${THRIFT_SRCS} ${THRIFT_HDRS})

if (NOT THRIFT_LIBRARY)
    message(FATAL_ERROR "Thrift library not found")
endif()

# sasl2
find_library(SASL2_LIBRARY sasl2)
if (NOT SASL2_LIBRARY)
    message(FATAL_ERROR "libsasl2 not found")
endif()

file(GLOB MY_PROJECT_SOURCES "src/*.cpp")
add_executable(${PROJECT_NAME} ${MY_PROJECT_SOURCES} ${THRIFT_SRCS})
add_dependencies(${PROJECT_NAME} gen-thrift-cpp)

target_compile_options(${PROJECT_NAME} PRIVATE -static-libstdc++)
target_link_options(${PROJECT_NAME} PRIVATE -static-libstdc++)
target_include_directories(${PROJECT_NAME} PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/include ${GEN_DIR}/)

target_link_libraries(${PROJECT_NAME} PRIVATE ${THRIFT_LIBRARY} ${SASL2_LIBRARY})
