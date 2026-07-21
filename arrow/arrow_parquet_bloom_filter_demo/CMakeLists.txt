cmake_minimum_required(VERSION 3.25)

project(arrow_parquet_bloom_filter_demo)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED True)

add_compile_options(-O3 -Wall)

option(ENABLE_ASAN "Enable AddressSanitizer" OFF)

if(NOT DEFINED CMAKE_BUILD_TYPE OR CMAKE_BUILD_TYPE STREQUAL "")
    set(CMAKE_BUILD_TYPE Release CACHE STRING "Choose the type of build." FORCE)
endif()

if(ENABLE_ASAN)
    add_compile_options(-fsanitize=address -g)
    add_link_options(-fsanitize=address -static-libasan)
endif()

set(ARROW_DEFINE_OPTIONS ON CACHE BOOL "Define Arrow build options" FORCE)
set(ARROW_BUILD_SHARED OFF CACHE BOOL "Build Arrow shared libraries" FORCE)
set(ARROW_BUILD_STATIC ON CACHE BOOL "Build Arrow static libraries" FORCE)
set(ARROW_PARQUET ON CACHE BOOL "Build Parquet libraries" FORCE)
set(ARROW_DATASET OFF CACHE BOOL "Build Arrow Dataset libraries" FORCE)
set(ARROW_BUILD_TESTS OFF CACHE BOOL "Build Arrow tests" FORCE)
set(ARROW_BUILD_BENCHMARKS OFF CACHE BOOL "Build Arrow benchmarks" FORCE)
set(PARQUET_BUILD_EXAMPLES OFF CACHE BOOL "Build Parquet examples" FORCE)
set(PARQUET_BUILD_EXECUTABLES OFF CACHE BOOL "Build Parquet executables" FORCE)

set(ARROW_CPP_SOURCE_DIR ${CMAKE_SOURCE_DIR}/contrib/arrow/cpp)
set(ARROW_CPP_BINARY_DIR ${CMAKE_BINARY_DIR}/arrow_build)

add_subdirectory(${CMAKE_SOURCE_DIR}/contrib/arrow/cpp ${CMAKE_BINARY_DIR}/arrow_build)

add_executable(ParquetWithBloomFilterWriter
    ${CMAKE_SOURCE_DIR}/cpp/ParquetWithBloomFilterWriter.cpp)

add_executable(ParquetWithBloomFilterReader
    ${CMAKE_SOURCE_DIR}/cpp/ParquetWithBloomFilterReader.cpp)

target_compile_options(ParquetWithBloomFilterWriter PRIVATE -static-libstdc++)
target_link_options(ParquetWithBloomFilterWriter PRIVATE -static-libstdc++)

target_compile_options(ParquetWithBloomFilterReader PRIVATE -static-libstdc++)
target_link_options(ParquetWithBloomFilterReader PRIVATE -static-libstdc++)

target_include_directories(ParquetWithBloomFilterWriter PRIVATE
    ${ARROW_CPP_SOURCE_DIR}/src
    ${ARROW_CPP_BINARY_DIR}/src)

target_include_directories(ParquetWithBloomFilterReader PRIVATE
    ${ARROW_CPP_SOURCE_DIR}/src
    ${ARROW_CPP_BINARY_DIR}/src)

target_link_libraries(ParquetWithBloomFilterWriter PRIVATE parquet_static arrow_static)
# target_link_libraries(ParquetWithBloomFilterWriter PRIVATE parquet_shared arrow_shared)

target_link_libraries(ParquetWithBloomFilterReader PRIVATE parquet_static arrow_static)
# target_link_libraries(ParquetWithBloomFilterReader PRIVATE parquet_shared arrow_shared)
