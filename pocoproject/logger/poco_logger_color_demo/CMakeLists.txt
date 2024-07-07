cmake_minimum_required(VERSION 3.20)

project(poco_logger_color_demo)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED True)

set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -O3 -Wall")

file(GLOB MY_PROJECT_SOURCES "*.cpp")
add_executable(${PROJECT_NAME} ${MY_PROJECT_SOURCES})

target_compile_options(${PROJECT_NAME} PRIVATE -static-libstdc++)
target_link_options(${PROJECT_NAME} PRIVATE -static-libstdc++)

find_package(Poco REQUIRED COMPONENTS Foundation Net XML JSON)
target_link_libraries(${PROJECT_NAME} Poco::Foundation Poco::Net Poco::XML Poco::JSON)
