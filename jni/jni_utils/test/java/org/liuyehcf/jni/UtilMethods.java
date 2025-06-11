package org.liuyehcf.jni;

import java.util.HashMap;

public class UtilMethods {
    public static String getString() {
        return "Hello, JNI!";
    }

    public static byte[] getBytes() {
        return getString().getBytes();
    }

    public static void print(Object obj) {
        if (obj == null) {
            throw new NullPointerException();
        }
        if (obj instanceof byte[]) {
            System.out.println("JavaPrint: " + new String((byte[]) obj));
        } else {
            System.out.println("JavaPrint: " + obj);
        }
    }

    public static HashMap<String, String> getHashMap() {
        HashMap<String, String> map = new HashMap<>();
        map.put("key1", "value1");
        map.put("key2", "value2");
        return map;
    }
}
