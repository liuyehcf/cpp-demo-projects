cmake_minimum_required(VERSION 3.20)

project(coredump_demo)

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

set(ALL_LIBS
    ${UNWIND_LIBRARY}
    dl
    )
message(STATUS "ALL_LIBS: ${ALL_LIBS}")

file(GLOB SOURCE ${CMAKE_SOURCE_DIR}/src/*.cpp)
add_executable(${PROJECT_NAME} ${SOURCE})
target_include_directories(${PROJECT_NAME} PRIVATE ${UNWIND_INCLUDE_DIR})
# -DUNW_LOCAL_ONLY is mandatory, otherwise some link error may occur, like:
#    undefined reference to `_Ux86_64_init_local'
#    undefined reference to `_Ux86_64_get_reg'
#    undefined reference to `_Ux86_64_get_proc_name'
#    undefined reference to `_Ux86_64_step'
target_compile_options(${PROJECT_NAME} PRIVATE -DUNW_LOCAL_ONLY -g -ggdb)
# -rdynamic exports executable symbols so dladdr() and backtrace can resolve them.
target_link_options(${PROJECT_NAME} PRIVATE -rdynamic)
target_link_libraries(${PROJECT_NAME} PRIVATE ${ALL_LIBS})
