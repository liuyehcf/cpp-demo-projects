cmake_minimum_required(VERSION 3.20)
project(lance_rust_ffi_demo)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED True)

add_compile_options(-O3 -Wall)

option(ENABLE_ASAN "Enable AddressSanitizer" OFF)
if(ENABLE_ASAN)
    add_compile_options(-fsanitize=address -g)
    add_link_options(-fsanitize=address -static-libasan)
endif()

# Find Arrow
find_package(Arrow REQUIRED)
find_package(ArrowDataset REQUIRED)

# Set build dir for rust
set(RUST_BUILD_DIR "${CMAKE_BINARY_DIR}/rust")
set(RUST_LIB_DIR "${RUST_BUILD_DIR}/release")
set(RUST_LIB_NAME "lance_ffi")
set(RUST_LIB_FILE "${RUST_LIB_DIR}/lib${RUST_LIB_NAME}.so")

# Create the executable
file(GLOB PROJECT_SOURCE ${CMAKE_SOURCE_DIR}/cpp/src/*.cpp)
add_executable(${PROJECT_NAME} ${PROJECT_SOURCE})
target_include_directories(${PROJECT_NAME} PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/cpp/include)

# Link the Rust library and Arrow
target_link_libraries(${PROJECT_NAME} ${RUST_LIB_FILE} Arrow::arrow_shared ArrowDataset::arrow_dataset_shared)

# Add custom target to build Rust library first
add_custom_target(rust_lib
    COMMAND cargo build --release --target-dir ${RUST_BUILD_DIR}
    WORKING_DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR}/rust
    COMMENT "Building Rust library"
)

# Make sure Rust library is built before C++ executable
add_dependencies(${PROJECT_NAME} rust_lib)

# Copy the dynamic library to the build directory
add_custom_command(TARGET ${PROJECT_NAME} POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E copy_if_different
    "${RUST_LIB_FILE}"
    $<TARGET_FILE_DIR:${PROJECT_NAME}>)

# On Linux, set RPATH to find the shared library
set_target_properties(${PROJECT_NAME} PROPERTIES
    INSTALL_RPATH "$ORIGIN"
    BUILD_WITH_INSTALL_RPATH TRUE)
