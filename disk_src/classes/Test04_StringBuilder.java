/* Test04_StringBuilder.java — String / StringBuilder 조작 */
public class Test04_StringBuilder {
    public static void main(String[] args) {
        System.out.println("=== Test04: StringBuilder ===");

        StringBuilder sb = new StringBuilder();
        sb.append("Hello");
        sb.append(", ");
        sb.append("ParinOS");
        sb.append("!");
        System.out.println(sb.toString());   /* Hello, ParinOS! */

        /* int append */
        StringBuilder sb2 = new StringBuilder();
        sb2.append("Answer=");
        sb2.append(42);
        System.out.println(sb2.toString());  /* Answer=42 */

        /* String concat (compiler turns into StringBuilder) */
        String s = "The year is " + 2025;
        System.out.println(s);

        /* length */
        System.out.println(sb.toString().length()); /* 17 */

        System.out.println("Test04 PASSED");
    }
}
