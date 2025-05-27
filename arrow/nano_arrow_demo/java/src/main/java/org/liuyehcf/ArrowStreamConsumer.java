package org.liuyehcf;

import org.apache.arrow.c.ArrowArrayStream;
import org.apache.arrow.c.Data;
import org.apache.arrow.memory.RootAllocator;
import org.apache.arrow.vector.VarCharVector;
import org.apache.arrow.vector.VectorSchemaRoot;
import org.apache.arrow.vector.ipc.ArrowReader;

import java.io.IOException;

public class ArrowStreamConsumer {
    public static void consume(long address) throws IOException {
        System.out.println("[java] Step1: Received stream address: " + address);
        try (ArrowArrayStream stream = ArrowArrayStream.wrap(address)) {
            System.out.println("[java] Step2: Wrapped ArrowArrayStream");
            try (ArrowReader arrowReader = Data.importArrayStream(new RootAllocator(), stream)) {
                System.out.println("[java] Step3: Created ArrowReader");
                while (arrowReader.loadNextBatch()) {
                    System.out.println("[java] Step4: Loaded batch");
                    try (VectorSchemaRoot root = arrowReader.getVectorSchemaRoot()) {
                        System.out.println("[java] Step5: Got VectorSchemaRoot");
                        VarCharVector colStr = (VarCharVector) root.getVector("col_str");
                        System.out.println("[java] Read values:" + colStr.getValueCount());
                        for (int i = 0; i < colStr.getValueCount(); i++) {
                            System.out.printf("  %d: %s\n", i, colStr.getObject(i));
                        }
                    }
                }
                System.out.println("[java] Step6: Finished loading batches");
            }
            System.out.println("[java] Step7: Reader closed");
        }
    }
}
