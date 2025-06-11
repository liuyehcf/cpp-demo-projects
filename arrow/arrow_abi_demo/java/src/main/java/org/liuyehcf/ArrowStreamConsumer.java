package org.liuyehcf;

import org.apache.arrow.c.ArrowArrayStream;
import org.apache.arrow.c.Data;
import org.apache.arrow.memory.RootAllocator;
import org.apache.arrow.vector.FieldVector;
import org.apache.arrow.vector.VectorSchemaRoot;
import org.apache.arrow.vector.ipc.ArrowReader;

import java.io.IOException;
import java.util.List;

public class ArrowStreamConsumer {
    public static void consume(long address) throws IOException {
        System.out.println("[java] Step1: Receive stream address: " + address);
        try (ArrowArrayStream stream = ArrowArrayStream.wrap(address)) {
            System.out.println("[java] Step2: Wrap ArrowArrayStream");
            // VectorSchemaRoot must be hold until all data is read, otherwise it may fail to
            // read struct or other complex types.
            try (ArrowReader arrowReader = Data.importArrayStream(new RootAllocator(), stream);
                    VectorSchemaRoot root = arrowReader.getVectorSchemaRoot()) {
                System.out.println("[java] Step3: Create ArrowReader and VectorSchemaRoot");
                int batchId = 0;
                while (arrowReader.loadNextBatch()) {
                    System.out.println(
                            "[java] Step4: Loade batch ("
                                    + batchId++
                                    + "): "
                                    + root.getRowCount()
                                    + " rows");
                    List<FieldVector> vectors = root.getFieldVectors();
                    for (int i = 0; i < root.getRowCount(); i++) {
                        System.out.printf("  %d:\n", i);
                        for (FieldVector vector : vectors) {
                            System.out.printf(
                                    "    %s: %s\n", vector.getName(), vector.getObject(i));
                        }
                    }
                }
                System.out.println("[java] Step6: Finish loading batches");
            }
            System.out.println("[java] Step7: Close ArrowReader and VectorSchemaRoot");
        }
        System.out.println("[java] Step8: Close ArrowArrayStream");
    }
}
