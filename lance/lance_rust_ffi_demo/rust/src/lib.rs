use anyhow::Result;
use arrow::array::{Array, Int32Array, StringArray};
use arrow::datatypes::{DataType, Field, Schema};
use arrow::record_batch::RecordBatch;
use std::sync::Arc;

pub mod ffi;
pub mod lance_operations;

pub use lance_operations::*;

/// Sample data structure for demonstration
#[derive(Debug, Clone)]
pub struct SampleData {
    pub id: i32,
    pub name: String,
    pub value: i32,
}

impl SampleData {
    pub fn new(id: i32, name: String, value: i32) -> Self {
        Self { id, name, value }
    }
}

/// Create a sample Arrow schema for our demo data
pub fn create_sample_schema() -> Arc<Schema> {
    Arc::new(Schema::new(vec![
        Field::new("id", DataType::Int32, false),
        Field::new("name", DataType::Utf8, false),
        Field::new("value", DataType::Int32, false),
    ]))
}

/// Convert sample data to Arrow RecordBatch
pub fn sample_data_to_record_batch(data: &[SampleData]) -> Result<RecordBatch> {
    let schema = create_sample_schema();

    let ids: Vec<i32> = data.iter().map(|d| d.id).collect();
    let names: Vec<String> = data.iter().map(|d| d.name.clone()).collect();
    let values: Vec<i32> = data.iter().map(|d| d.value).collect();

    let id_array = Arc::new(Int32Array::from(ids));
    let name_array = Arc::new(StringArray::from(names));
    let value_array = Arc::new(Int32Array::from(values));

    let batch = RecordBatch::try_new(schema, vec![id_array, name_array, value_array])?;

    Ok(batch)
}

/// Convert Arrow RecordBatch to sample data
pub fn record_batch_to_sample_data(batch: &RecordBatch) -> Result<Vec<SampleData>> {
    let mut data = Vec::new();

    let id_column = batch
        .column(0)
        .as_any()
        .downcast_ref::<Int32Array>()
        .ok_or_else(|| anyhow::anyhow!("Failed to downcast id column"))?;
    let name_column = batch
        .column(1)
        .as_any()
        .downcast_ref::<StringArray>()
        .ok_or_else(|| anyhow::anyhow!("Failed to downcast name column"))?;
    let value_column = batch
        .column(2)
        .as_any()
        .downcast_ref::<Int32Array>()
        .ok_or_else(|| anyhow::anyhow!("Failed to downcast value column"))?;

    for i in 0..batch.num_rows() {
        let id = id_column.value(i);
        let name = name_column.value(i).to_string();
        let value = value_column.value(i);

        data.push(SampleData::new(id, name, value));
    }

    Ok(data)
}
