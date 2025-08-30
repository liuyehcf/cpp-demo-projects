use crate::{LanceTableManager, SampleData};
use arrow::{ffi_stream::FFI_ArrowArrayStream, record_batch::RecordBatchIterator};
use std::ffi::CStr;
use std::os::raw::{c_char, c_int};
use std::sync::{Mutex, OnceLock};
use tokio::runtime::Runtime;

// Global runtime for async operations
static RUNTIME: OnceLock<Runtime> = OnceLock::new();
static MANAGER: OnceLock<Mutex<LanceTableManager>> = OnceLock::new();

// Initialize the Lance manager and runtime
#[no_mangle]
pub extern "C" fn lance_init(db_path: *const c_char) -> c_int {
    // Check if already initialized
    if RUNTIME.get().is_some() {
        return 1; // Already initialized
    }

    let rt = match Runtime::new() {
        Ok(rt) => rt,
        Err(_) => return -1,
    };

    let path_str = unsafe {
        match CStr::from_ptr(db_path).to_str() {
            Ok(s) => s,
            Err(_) => return -2,
        }
    };

    // Create dataset directory if it doesn't exist
    eprintln!("Creating directory: {}", path_str);
    if let Err(e) = std::fs::create_dir_all(path_str) {
        eprintln!("Failed to create directory: {}", e);
        return -4;
    }
    eprintln!("Directory created successfully: {}", path_str);

    let manager = LanceTableManager::new(path_str);

    // Set the runtime and manager using OnceLock
    if RUNTIME.set(rt).is_err() {
        return -5; // Failed to set runtime
    }
    if MANAGER.set(Mutex::new(manager)).is_err() {
        return -6; // Failed to set manager
    }

    0 // Success
}

// Create a new Lance table
#[no_mangle]
pub extern "C" fn lance_create_table(table_name: *const c_char) -> c_int {
    let rt = match RUNTIME.get() {
        Some(rt) => rt,
        None => return -1, // Not initialized
    };

    let manager = match MANAGER.get() {
        Some(m) => m,
        None => return -1,
    };

    let name_str = unsafe {
        match CStr::from_ptr(table_name).to_str() {
            Ok(s) => s,
            Err(_) => return -2,
        }
    };

    // Create sample data for table schema - need at least one meaningful record
    let _sample_data = vec![SampleData {
        id: 1,
        name: "initial".to_string(),
        value: 100,
    }];

    let manager_guard = match manager.lock() {
        Ok(guard) => guard,
        Err(_) => return -3,
    };

    match rt.block_on(manager_guard.create_table()) {
        Ok(_) => {
            eprintln!("Table '{}' created successfully", name_str);
            0
        }
        Err(e) => {
            eprintln!("Failed to create table '{}': {}", name_str, e);
            -3
        }
    }
}

/// Write Arrow stream data to a Lance table using FFI_ArrowArrayStream
#[no_mangle]
pub extern "C" fn lance_write_arrow_stream(
    table_name: *const c_char,
    stream: *mut FFI_ArrowArrayStream,
) -> c_int {
    let rt = match RUNTIME.get() {
        Some(rt) => rt,
        None => return -1,
    };

    let manager = match MANAGER.get() {
        Some(m) => m,
        None => return -1,
    };

    let table_name_str = unsafe {
        match CStr::from_ptr(table_name).to_str() {
            Ok(s) => s,
            Err(_) => return -2,
        }
    };

    if stream.is_null() {
        return -3;
    }

    // Convert FFI_ArrowArrayStream to RecordBatch
    let stream_reader = unsafe {
        match arrow::ffi_stream::ArrowArrayStreamReader::from_raw(&mut *stream) {
            Ok(reader) => reader,
            Err(e) => {
                eprintln!("Failed to create stream reader: {}", e);
                return -4;
            }
        }
    };

    let mut all_data = Vec::new();
    for batch_result in stream_reader {
        let batch = match batch_result {
            Ok(batch) => batch,
            Err(e) => {
                eprintln!("Failed to read batch from stream: {}", e);
                return -5;
            }
        };

        // Convert RecordBatch to SampleData
        let batch_data = match crate::record_batch_to_sample_data(&batch) {
            Ok(data) => data,
            Err(e) => {
                eprintln!("Failed to convert RecordBatch to SampleData: {}", e);
                return -6;
            }
        };
        all_data.extend(batch_data);
    }

    let sample_data = all_data;

    let mut manager_guard = match manager.lock() {
        Ok(guard) => guard,
        Err(_) => return -7,
    };

    // Open the table first
    if let Err(_) = rt.block_on(manager_guard.open_table()) {
        return -8; // Failed to open table
    }

    match rt.block_on(manager_guard.write_data(&sample_data)) {
        Ok(_) => {
            eprintln!(
                "Arrow stream data written successfully to table '{}'",
                table_name_str
            );
            0
        }
        Err(e) => {
            eprintln!("Failed to write Arrow stream data: {}", e);
            -3
        }
    }
}

/// Read data from a Lance table as Arrow stream using FFI_ArrowArrayStream
#[no_mangle]
pub extern "C" fn lance_read_arrow_stream(
    table_name: *const c_char,
    stream: *mut FFI_ArrowArrayStream,
) -> c_int {
    let rt = match RUNTIME.get() {
        Some(rt) => rt,
        None => return -1,
    };

    let manager = match MANAGER.get() {
        Some(m) => m,
        None => return -1,
    };

    let table_name_str = unsafe {
        match CStr::from_ptr(table_name).to_str() {
            Ok(s) => s,
            Err(_) => return -2,
        }
    };

    if stream.is_null() {
        return -3;
    }

    let mut manager_guard = match manager.lock() {
        Ok(guard) => guard,
        Err(_) => return -4,
    };

    // Open the table first
    if let Err(_) = rt.block_on(manager_guard.open_table()) {
        return -5; // Failed to open table
    }

    match rt.block_on(manager_guard.read_all_data()) {
        Ok(data) => {
            // Convert SampleData to RecordBatch
            let batch = match crate::sample_data_to_record_batch(&data) {
                Ok(batch) => batch,
                Err(e) => {
                    eprintln!("Failed to convert SampleData to RecordBatch: {}", e);
                    return -4;
                }
            };

            // Create an iterator that yields the single batch
            let batches = vec![batch];

            // Create RecordBatchIterator from batches
            let schema = batches[0].schema();
            let batch_results: Vec<
                Result<arrow::record_batch::RecordBatch, arrow::error::ArrowError>,
            > = batches.into_iter().map(Ok).collect();
            let batch_iter = RecordBatchIterator::new(batch_results.into_iter(), schema);

            // Export to FFI_ArrowArrayStream using new method
            unsafe {
                *stream = FFI_ArrowArrayStream::new(Box::new(batch_iter));
            }
            eprintln!(
                "Data read successfully from table '{}' as Arrow stream",
                table_name_str
            );
            0
        }
        Err(e) => {
            eprintln!("Failed to read data: {}", e);
            -6
        }
    }
}

// Note: FFI_ArrowArrayStream handles its own memory management,
// so no explicit free function is needed for Arrow stream data

/// Cleanup resources
#[no_mangle]
pub extern "C" fn lance_cleanup() {
    // Note: OnceLock doesn't support clearing values once set
    // This is intentional as cleanup in FFI contexts can be problematic
    // The resources will be cleaned up when the process exits
    eprintln!("Cleanup called - resources will be freed on process exit");
}
