package org.liuyehcf.jni;

public class ThrowException {
    public void run1(Object obj) throws Throwable { nested1(); }
    public int run2(Object[] objs, int i) throws Throwable { nested1(); return 0; }
    public Object run3(double d, Object[] objs, boolean[] bs) throws Throwable { nested1(); return null; }
    public byte[][] run4(int[][] is, long l, Object obj, short[] ss) throws Throwable { nested1(); return null; }
    public void run5(boolean z, byte b, char c, short s, int i, long l, float f, double d, boolean[] zs, byte[][] bs, char[][][] cs, Object obj, Object[] objs) throws Throwable { nested1(); }
    public static void nested1() throws Exception {
        try {
            nested2();
        } catch (Exception e) {
            throw new Exception("Exception in nested1", e);
        }
    }
    public static void nested2() throws Exception {
        try {
            nested3();
        } catch (Exception e) {
            throw new Exception("Exception in nested2", e);
        }
    }
    public static void nested3() throws Exception {
        try {
            nested4();
        } catch (Exception e) {
            throw new Exception("Exception in nested3", e);
        }
    }
    public static void nested4() throws Exception {
        throw new Exception("Root cause exception in nested4");
    }
}
