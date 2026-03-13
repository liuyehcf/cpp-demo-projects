cmake_minimum_required(VERSION 3.20)

project(aws-sdk-cpp-demo)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED True)

# AWS SDK Configuration (MUST be set before add_subdirectory) ---
# CRITICAL: Tell the SDK to build bundled dependencies (aws-c-common, etc.)
# This avoids the "Could not find aws-c-common" error.
set(BUILD_DEPS ON CACHE BOOL "Build bundled dependencies" FORCE)
# Limit build to core and required services to save significant compilation time
set(BUILD_ONLY "s3;core" CACHE STRING "Build only specific services" FORCE)
set(ENABLE_TESTING OFF CACHE BOOL "Disable SDK unit tests" FORCE)
set(AUTORUN_UNIT_TESTS OFF CACHE BOOL "Do not run tests during build" FORCE)
# For Windows/MSVC: Ensure the SDK uses the shared C Runtime (/MD)
set(FORCE_SHARED_CRT ON CACHE BOOL "Use shared CRT" FORCE)
# Optional: Set to ON if you prefer Dynamic Libraries (.so/.dll) over Static (.a/.lib)
# set(BUILD_SHARED_LIBS ON CACHE BOOL "Build shared libraries" FORCE)

add_subdirectory(contrib/aws-sdk-cpp)

file(GLOB SOURCE ${CMAKE_SOURCE_DIR}/src/*.cpp)
add_executable(${PROJECT_NAME} ${SOURCE})

target_link_libraries(${PROJECT_NAME}
    PRIVATE
    aws-cpp-sdk-s3
    aws-cpp-sdk-core
)
