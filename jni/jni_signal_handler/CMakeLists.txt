cmake_minimum_required(VERSION 3.20)

project(jni_signal_handler)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED True)

find_path(UNWIND_INCLUDE_DIR libunwind.h
    PATHS /usr/include /usr/local/include
)
find_library(UNWIND_LIBRARY unwind
    PATHS /usr/lib /usr/local/lib
)
message(STATUS "UNWIND_LIBRARY: ${UNWIND_LIBRARY}")
message(STATUS "UNWIND_INCLUDE_DIR: ${UNWIND_INCLUDE_DIR}")

add_subdirectory(${CMAKE_SOURCE_DIR}/../jni_utils ${CMAKE_BINARY_DIR}/jni_utils_build)

set(ALL_LIBS
    ${UNWIND_LIBRARY}
    jni_utils
    )
message(STATUS "ALL_LIBS: ${ALL_LIBS}")

# Java
file(GLOB_RECURSE JAVA_SOURCES "${CMAKE_SOURCE_DIR}/src/java/*.java")
message(STATUS "JAVA_SOURCES: ${JAVA_SOURCES}")
add_custom_target(build_java
    COMMAND ${CMAKE_COMMAND} -E make_directory ${CMAKE_BINARY_DIR}/classes
    COMMAND javac -d ${CMAKE_BINARY_DIR}/classes ${JAVA_SOURCES}
    DEPENDS ${JAVA_SOURCES}
    WORKING_DIRECTORY ${CMAKE_SOURCE_DIR}/src/java
)

file(GLOB SOURCE ${CMAKE_SOURCE_DIR}/src/*.cpp)
add_executable(${PROJECT_NAME} ${SOURCE})
target_include_directories(${PROJECT_NAME} PRIVATE ${UNWIND_INCLUDE_DIR})
# -DUNW_LOCAL_ONLY is mandatory, otherwise some link error may occur, like:
#    undefined reference to `_Ux86_64_init_local'
#    undefined reference to `_Ux86_64_get_reg'
#    undefined reference to `_Ux86_64_get_proc_name'
#    undefined reference to `_Ux86_64_step'
target_compile_options(${PROJECT_NAME} PRIVATE -DUNW_LOCAL_ONLY)
target_link_libraries(${PROJECT_NAME} PRIVATE ${ALL_LIBS})
add_dependencies(${PROJECT_NAME} build_java)
