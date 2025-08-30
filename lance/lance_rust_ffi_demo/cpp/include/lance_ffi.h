#ifndef LANCE_DEMO_H
#define LANCE_DEMO_H

#include <arrow/c/abi.h>

#ifdef __cplusplus
extern "C" {
#endif

// Initialize the Lance manager with database path
// Returns: 0 on success, negative on error
int lance_init(const char* db_path);

// Create a new Lance table
// Returns: 0 on success, negative on error
int lance_create_table(const char* table_name);

// Write Arrow stream data to a Lance table using ArrowArrayStream
int lance_write_arrow_stream(const char* table_name, struct ArrowArrayStream* stream);

// Read data from a Lance table as ArrowArrayStream
int lance_read_arrow_stream(const char* table_name, struct ArrowArrayStream* stream);

// Cleanup resources
void lance_cleanup();

#ifdef __cplusplus
}
#endif

#endif // LANCE_DEMO_H
