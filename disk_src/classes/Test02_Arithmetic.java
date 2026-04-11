/* Test02_Arithmetic.java — 정수 연산 및 문자열 출력 */
public class Test02_Arithmetic {
    public static void main(String[] args) {
        System.out.println("=== Test02: Arithmetic ===");

        int a = 100;
        int b = 37;

        int sum  = a + b;
        int diff = a - b;
        int prod = a * b;
        int quot = a / b;
        int rem  = a % b;

        System.out.println(sum);
        System.out.println(diff);
        System.out.println(prod);
        System.out.println(quot);
        System.out.println(rem);

        /* 음수 처리 */
        int neg = -42;
        System.out.println(neg);

        /* 0 경계값 */
        System.out.println(0);

        System.out.println("Test02 PASSED");
    }
}
