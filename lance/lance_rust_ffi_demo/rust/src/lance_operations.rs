use anyhow::Result;
use arrow::record_batch::RecordBatchIterator;
use futures::TryStreamExt;
use std::sync::Arc;

use crate::{create_sample_schema, record_batch_to_sample_data, SampleData};
use lance::Dataset;

/// Lance table operations for creating, writing, and reading data
pub struct LanceTableManager {
    path: String,
    dataset: Option<Dataset>,
}

impl LanceTableManager {
    /// Create a new LanceTableManager with a path to the dataset
    pub fn new(path: &str) -> Self {
        Self {
            path: path.to_string(),
            dataset: None,
        }
    }

    /// Open the dataset
    pub async fn open_table(&mut self) -> Result<()> {
        if self.dataset.is_some() {
            // Already opened
            return Ok(());
        }
        self.dataset = Some(Dataset::open(&self.path).await?);
        Ok(())
    }

    /// Create a new Lance table with the given schema
    pub async fn create_table(&self) -> Result<()> {
        let schema = create_sample_schema();

        // Create an empty table with the schema using Lance API
        let empty_batches = RecordBatchIterator::new(std::iter::empty(), schema);

        Dataset::write(empty_batches, &self.path, None).await?;

        Ok(())
    }

    /// Write existing table with new data from RecordBatchReader
    pub async fn write_from_stream(
        &mut self,
        reader: Box<dyn arrow::record_batch::RecordBatchReader + Send + 'static>,
        is_overwrite: bool,
    ) -> Result<()> {
        let write_params = lance::dataset::WriteParams {
            mode: if is_overwrite {
                lance::dataset::WriteMode::Overwrite
            } else {
                lance::dataset::WriteMode::Append
            },
            ..lance::dataset::WriteParams::default()
        };

        println!("[rust]: Starting to write table with stream data...");
        Dataset::write(
            reader,
            lance::dataset::WriteDestination::Dataset(Arc::new(self.dataset.clone().unwrap())),
            Some(write_params),
        )
        .await?;
        println!("[rust]: Stream data written successfully.");

        // Reload the dataset to get the latest version and avoid commit conflicts
        self.dataset = Some(Dataset::open(&self.path).await?);
        println!("[rust]: Dataset reloaded after write operation.");

        Ok(())
    }

    /// Read all data from a Lance table using low-level Lance API
    pub async fn read_all_data(&self) -> Result<Vec<SampleData>> {
        if let Some(dataset) = &self.dataset {
            // Create a scanner and convert to stream
            let scanner = dataset.scan();
            let mut stream = scanner.try_into_stream().await?;
            println!("[rust]: Scanner created, start to read data...");

            let mut results = Vec::new();
            while let Some(batch) = stream.try_next().await? {
                let sample_data = record_batch_to_sample_data(&batch)?;
                results.extend(sample_data);
            }

            println!("[rust]: Finished reading all data.");
            Ok(results)
        } else {
            Err(anyhow::anyhow!("No dataset opened. Call open_table first."))
        }
    }

    /// Read filtered data from a Lance table using low-level Lance API
    pub async fn read_filtered_data(&self, filter: &str) -> Result<Vec<SampleData>> {
        if let Some(dataset) = &self.dataset {
            // Create a scanner with filter and convert to stream
            let mut scan = dataset.scan();
            let scanner = scan.filter(filter)?;
            let mut stream = scanner.try_into_stream().await?;

            let mut results = Vec::new();
            while let Some(batch) = stream.try_next().await? {
                let sample_data = record_batch_to_sample_data(&batch)?;
                results.extend(sample_data);
            }

            Ok(results)
        } else {
            Err(anyhow::anyhow!("No dataset opened. Call open_table first."))
        }
    }

    /// Get table schema information
    pub async fn get_table_schema(&self) -> Result<arrow::datatypes::SchemaRef> {
        if let Some(dataset) = &self.dataset {
            let schema = dataset.schema();
            // Convert Lance fields to Arrow fields
            let arrow_fields: Vec<arrow::datatypes::Field> = schema
                .fields
                .iter()
                .map(|lance_field| {
                    arrow::datatypes::Field::new(
                        &lance_field.name,
                        lance_field.data_type().clone().into(),
                        lance_field.nullable,
                    )
                })
                .collect();
            let arrow_schema = Arc::new(arrow::datatypes::Schema::new(arrow_fields));
            Ok(arrow_schema)
        } else {
            Err(anyhow::anyhow!("No dataset opened. Call open_table first."))
        }
    }

    /// Delete the table
    pub async fn drop_table(&self) -> Result<()> {
        std::fs::remove_dir_all(&self.path)?;
        println!("[rust]: Dropped table at path '{}'", self.path);
        Ok(())
    }
}
