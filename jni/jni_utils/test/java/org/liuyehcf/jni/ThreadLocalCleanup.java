package org.liuyehcf.jni;

import java.lang.ref.WeakReference;
import java.nio.ByteBuffer;
import java.util.concurrent.atomic.AtomicInteger;

public class ThreadLocalCleanup {
    private static final int RESOURCE_SIZE = 4 * 1024 * 1024;
    private static final AtomicInteger CLEANUP_COUNT = new AtomicInteger();

    private static final ThreadLocal<CachedResource> CACHE = new ThreadLocal<CachedResource>() {
        @Override
        protected CachedResource initialValue() {
            return new CachedResource();
        }
    };

    private static volatile WeakReference<CachedResource> resourceReference;
    private static volatile WeakReference<Thread> threadReference;

    public static void cacheResource() {
        CachedResource resource = CACHE.get();
        resource.touch();
        resourceReference = new WeakReference<>(resource);
        threadReference = new WeakReference<>(Thread.currentThread());
        System.out.println("ThreadLocal cached resource created on thread: " + Thread.currentThread().getName());
    }

    public static int getCleanupCount() {
        return CLEANUP_COUNT.get();
    }

    public static boolean isResourceAlive() {
        WeakReference<CachedResource> reference = resourceReference;
        return reference != null && reference.get() != null;
    }

    public static boolean isThreadReachable() {
        WeakReference<Thread> reference = threadReference;
        return reference != null && reference.get() != null;
    }

    private static class CachedResource {
        private final String ownerThreadName = Thread.currentThread().getName();
        private final ByteBuffer buffer = ByteBuffer.allocateDirect(RESOURCE_SIZE);

        private void touch() {
            buffer.put(0, (byte) 1);
        }

        @Override
        protected void finalize() throws Throwable {
            try {
                CLEANUP_COUNT.incrementAndGet();
                System.out.println("ThreadLocal cached resource cleaned for thread: " + ownerThreadName);
            } finally {
                super.finalize();
            }
        }
    }
}
