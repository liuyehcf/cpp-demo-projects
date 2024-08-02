cmake_minimum_required(VERSION 3.20)

project(protobuf_demo)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED True)

add_compile_options(-O3 -Wall)

# protobuf
find_package(Protobuf REQUIRED)

# Generate protobuf
file(GLOB PROTO_DEFINITION ${CMAKE_SOURCE_DIR}/src/proto/*.proto)
protobuf_generate_cpp(PROTO_SOURCE PROTO_HEADER ${PROTO_DEFINITION})

file(GLOB PROJECT_SOURCE ${CMAKE_SOURCE_DIR}/src/*.cpp)
add_executable(${PROJECT_NAME} ${PROJECT_SOURCE} ${PROTO_SOURCE})
target_include_directories(${PROJECT_NAME} PRIVATE ${PROTOBUF_INCLUDE_DIRS} ${CMAKE_BINARY_DIR})
target_link_libraries(${PROJECT_NAME} PRIVATE ${PROTOBUF_LIBRARIES})
