cmake_minimum_required(VERSION 3.20)

project(paimon_hash)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED True)

add_compile_options(-O3 -Wall)

file(GLOB TEST_SOURCES "${CMAKE_CURRENT_SOURCE_DIR}/test/*.cpp")
add_executable(${PROJECT_NAME} ${TEST_SOURCES})

target_include_directories(${PROJECT_NAME} PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/include)

# Use local GTest from contrib directory
enable_testing()
add_subdirectory(contrib/googletest)

# Link GTest libraries from the subdirectory
target_link_libraries(${PROJECT_NAME} PRIVATE gtest gtest_main pthread)
