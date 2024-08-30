cmake_minimum_required(VERSION 3.20)
project(leveldb_demo)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED True)

add_compile_options(-O0 -Wall -g -ggdb)

add_subdirectory(contrib/leveldb)

file(GLOB MY_PROJECT_SOURCES "src/*.cpp")
add_executable(${PROJECT_NAME} ${MY_PROJECT_SOURCES})

target_compile_options(${PROJECT_NAME} PRIVATE -static-libstdc++)
target_link_options(${PROJECT_NAME} PRIVATE -static-libstdc++)

target_link_libraries(${PROJECT_NAME} PRIVATE leveldb)
