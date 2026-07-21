cmake_minimum_required(VERSION 3.20)

project(arrow_abi_demo)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED True)

add_compile_options(-O3 -Wall)

option(ENABLE_ASAN "Enable AddressSanitizer" OFF)

if(ENABLE_ASAN)
    add_compile_options(-fsanitize=address -g)
    add_link_options(-fsanitize=address -static-libasan)
endif()

find_package(Arrow REQUIRED)
add_subdirectory(${CMAKE_SOURCE_DIR}/../../jni/jni_utils ${CMAKE_BINARY_DIR}/jni_utils_build)

find_program(MAVEN_EXECUTABLE mvn)
find_program(JAVA_EXECUTABLE java)

if(NOT JAVA_EXECUTABLE)
    message(FATAL_ERROR "Java executable not found. Please install Java 17 or newer.")
endif()

execute_process(
    COMMAND ${JAVA_EXECUTABLE} -version
    OUTPUT_VARIABLE JAVA_VERSION_OUTPUT
    ERROR_VARIABLE JAVA_VERSION_OUTPUT
    OUTPUT_STRIP_TRAILING_WHITESPACE
    ERROR_STRIP_TRAILING_WHITESPACE
)

string(REGEX MATCH "\"([0-9]+)(\\.[^\"]*)?\"" JAVA_VERSION_MATCH "${JAVA_VERSION_OUTPUT}")

if(NOT JAVA_VERSION_MATCH)
    message(FATAL_ERROR "Failed to parse Java version from output: ${JAVA_VERSION_OUTPUT}")
endif()

set(JAVA_MAJOR_VERSION ${CMAKE_MATCH_1})

if(JAVA_MAJOR_VERSION LESS 17)
    message(FATAL_ERROR "Java 17 or newer is required, but found Java ${JAVA_MAJOR_VERSION}. Full version output: ${JAVA_VERSION_OUTPUT}")
endif()

set(JAVA_PROJECT_DIR ${CMAKE_SOURCE_DIR}/java)
set(JAVA_OUTPUT_DIR ${CMAKE_BINARY_DIR}/lib/jar)
set(JAR_NAME test-arrow-abi-jar-with-dependencies.jar)
set(JAR_PATH ${CMAKE_SOURCE_DIR}/java/target/${JAR_NAME})
file(GLOB_RECURSE JAVA_SOURCES ${JAVA_PROJECT_DIR}/src/main/*)
list(APPEND JAVA_SOURCES ${JAVA_PROJECT_DIR}/pom.xml)

message(STATUS "Java project dir: ${JAVA_PROJECT_DIR}")
message(STATUS "Java output dir: ${JAVA_OUTPUT_DIR}")
message(STATUS "Jar path: ${JAR_PATH}")
message(STATUS "Java sources: ${JAVA_SOURCES}")

add_custom_command(
    OUTPUT ${JAR_PATH}
    COMMAND ${MAVEN_EXECUTABLE} clean package -DskipTests
    WORKING_DIRECTORY ${JAVA_PROJECT_DIR}
    DEPENDS ${JAVA_SOURCES}
    COMMENT "Building Java project with Maven"
    VERBATIM
)
add_custom_target(build_java
    DEPENDS ${JAR_PATH}
    VERBATIM
)
add_custom_command(TARGET build_java POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E make_directory ${JAVA_OUTPUT_DIR}
    COMMAND ${CMAKE_COMMAND} -E copy ${JAR_PATH} ${JAVA_OUTPUT_DIR}/${JAR_NAME}
    COMMENT "Copying build artifacts to output directory"
)

file(GLOB PROJECT_SOURCE ${CMAKE_SOURCE_DIR}/cpp/*.cpp)
add_executable(${PROJECT_NAME} ${PROJECT_SOURCE})

target_compile_options(${PROJECT_NAME} PRIVATE -static-libstdc++)
target_link_options(${PROJECT_NAME} PRIVATE -static-libstdc++)

target_link_libraries(${PROJECT_NAME} PRIVATE jni_utils arrow_shared)

add_dependencies(${PROJECT_NAME} build_java)
