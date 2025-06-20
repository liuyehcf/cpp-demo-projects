package org.liuyehcf;

import org.apache.arrow.c.ArrowArrayStream;
import org.apache.arrow.c.Data;
import org.apache.arrow.memory.RootAllocator;
import org.apache.arrow.vector.IntVector;
import org.apache.arrow.vector.VectorSchemaRoot;
import org.apache.arrow.vector.VectorUnloader;
import org.apache.arrow.vector.ipc.ArrowReader;
import org.apache.arrow.vector.ipc.message.ArrowRecordBatch;
import org.apache.arrow.vector.types.pojo.ArrowType;
import org.apache.arrow.vector.types.pojo.Field;
import org.apache.arrow.vector.types.pojo.FieldType;
import org.apache.arrow.vector.types.pojo.Schema;

import java.util.Collections;
import java.util.Random;

public class ArrowStreamProvider extends ArrowReader {
    private final Schema schema;
    private final IntVector intVector;
    private final VectorSchemaRoot root;

    private final Random random = new Random();

    private int batchCount = 0;
    private boolean hasNext = true;
    private long bytesRead = 0;

    private ArrowStreamProvider() {
        super(new RootAllocator());
        this.schema =
                new Schema(
                        Collections.singletonList(
                                new Field(
                                        "int_col",
                                        FieldType.nullable(new ArrowType.Int(32, true)),
                                        null)));
        this.root = VectorSchemaRoot.create(schema, allocator);
        this.intVector = (IntVector) root.getVector("int_col");
    }

    @Override
    public boolean loadNextBatch() {
        System.out.println("[java] Calling loadNextBatch");

        if (++batchCount < 3) {
            nextBatch();
            return true;
        }

        return false;
    }

    private void nextBatch() {
        System.out.println("[java] Generate batch from java side");
        int rowNum = 5;
        intVector.allocateNew(rowNum);
        for (int i = 0; i < rowNum; i++) {
            bytesRead += 4;
            intVector.set(i, random.nextInt(100));
        }
        intVector.setValueCount(rowNum);
        root.setRowCount(rowNum);

        VectorUnloader unloader = new VectorUnloader(root);
        ArrowRecordBatch nextBatch = unloader.getRecordBatch();
        loadRecordBatch(nextBatch);

        System.out.println("[java] Write values:");
        for (int i = 0; i < intVector.getValueCount(); i++) {
            System.out.println("  " + i + ": " + intVector.get(i));
        }
    }

    @Override
    public long bytesRead() {
        System.out.println("[java] Calling bytesRead");
        return bytesRead;
    }

    @Override
    protected void closeReadSource() {
        System.out.println("[java] Calling closeReadSource");
        root.close();
        intVector.close();
        allocator.close();
    }

    @Override
    protected Schema readSchema() {
        System.out.println("[java] Calling readSchema");
        return schema;
    }

    public static void generate(long address) {
        ArrowStreamProvider provider = new ArrowStreamProvider();
        ArrowArrayStream stream = ArrowArrayStream.wrap(address);
        Data.exportArrayStream(provider.allocator, provider, stream);
    }
}
