use crate::{LanceTableManager, SampleData};
use arrow::{
    ffi_stream::FFI_ArrowArrayStream,
    record_batch::{RecordBatchIterator, RecordBatchReader},
};
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
        println!("[rust]: Already initialized");
        return 1;
    }

    let rt = Runtime::new().unwrap();
    let path_str = unsafe { CStr::from_ptr(db_path).to_str().unwrap() };

    // Create dataset directory if it doesn't exist
    std::fs::create_dir_all(path_str).unwrap();
    println!("[rust]: Directory created successfully: {}", path_str);

    let manager = LanceTableManager::new(path_str);

    // Set the runtime and manager using OnceLock
    RUNTIME.set(rt).unwrap();
    if MANAGER.set(Mutex::new(manager)).is_err() {
        return -6; // Failed to set manager
    }

    0
}

// Create a new Lance table
#[no_mangle]
pub extern "C" fn lance_create_table(table_name: *const c_char) -> c_int {
    let rt = RUNTIME.get().unwrap();
    let manager = MANAGER.get().unwrap();
    let name_str = unsafe { CStr::from_ptr(table_name).to_str().unwrap() };

    // Create sample data for table schema - need at least one meaningful record
    let _sample_data = vec![SampleData {
        id: 1,
        name: "initial".to_string(),
        value: 100,
    }];

    let manager_guard = manager.lock().unwrap();

    match rt.block_on(manager_guard.create_table()) {
        Ok(_) => {
            println!("[rust]: Table '{}' created successfully", name_str);
            0
        }
        Err(e) => {
            println!("[rust]: Failed to create table '{}': {}", name_str, e);
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
    let rt = RUNTIME.get().unwrap();
    let manager = MANAGER.get().unwrap();
    let table_name_str = unsafe { CStr::from_ptr(table_name).to_str().unwrap() };

    // Create a RecordBatchIterator directly from the FFI stream
    // This avoids buffering all data in memory
    let stream_reader =
        unsafe { arrow::ffi_stream::ArrowArrayStreamReader::from_raw(&mut *stream).unwrap() };

    // ArrowArrayStreamReader already implements RecordBatchReader
    // and is Send + 'static, so we can directly box it
    let batch_iter = Box::new(stream_reader) as Box<dyn RecordBatchReader + Send + 'static>;

    let mut manager_guard = manager.lock().unwrap();

    // Open the table first
    rt.block_on(manager_guard.open_table()).unwrap();

    // Use the new method to write directly from the stream without buffering
    match rt.block_on(manager_guard.write_from_stream(batch_iter)) {
        Ok(_) => {
            println!(
                "[rust]: Arrow stream data written successfully to table '{}'",
                table_name_str
            );
            0
        }
        Err(e) => {
            println!("[rust]: Failed to write Arrow stream data: {}", e);
            1
        }
    }
}

/// Read data from a Lance table as Arrow stream using FFI_ArrowArrayStream
#[no_mangle]
pub extern "C" fn lance_read_arrow_stream(
    table_name: *const c_char,
    stream: *mut FFI_ArrowArrayStream,
) -> c_int {
    let rt = RUNTIME.get().unwrap();
    let manager = MANAGER.get().unwrap();
    let table_name_str = unsafe { CStr::from_ptr(table_name).to_str().unwrap() };

    let mut manager_guard = manager.lock().unwrap();

    // Open the table first
    rt.block_on(manager_guard.open_table()).unwrap();

    match rt.block_on(manager_guard.read_all_data()) {
        Ok(data) => {
            // Convert SampleData to RecordBatch
            let batch = crate::sample_data_to_record_batch(&data).unwrap();
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
                // First initialize the stream with empty values
                std::ptr::write(stream, FFI_ArrowArrayStream::empty());

                // Try to create the stream
                let new_stream = FFI_ArrowArrayStream::new(Box::new(batch_iter));

                // Properly assign the stream to the pointer
                std::ptr::write(stream, new_stream);
            }
            println!(
                "[rust]: Data read successfully from table '{}' as Arrow stream",
                table_name_str
            );
            0
        }
        Err(e) => {
            println!("[rust]: Failed to read data: {}", e);
            1
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
    println!("[rust]: Cleanup called, resources will be freed on process exit");
}
