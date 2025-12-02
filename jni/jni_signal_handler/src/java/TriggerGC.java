public class TriggerGC {
    public static void trigger() {
        System.out.println("Requesting garbage collection...");
        System.gc();
        throw new RuntimeException();
    }
}
