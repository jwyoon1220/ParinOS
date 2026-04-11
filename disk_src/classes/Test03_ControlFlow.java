/* Test03_ControlFlow.java — if / for / while / 재귀 */
public class Test03_ControlFlow {

    static int factorial(int n) {
        if (n <= 1) return 1;
        return n * factorial(n - 1);
    }

    static int fib(int n) {
        if (n <= 1) return n;
        int a = 0, b = 1;
        for (int i = 2; i <= n; i++) {
            int tmp = a + b;
            a = b;
            b = tmp;
        }
        return b;
    }

    public static void main(String[] args) {
        System.out.println("=== Test03: Control Flow ===");

        /* for + if */
        int sum = 0;
        for (int i = 1; i <= 10; i++) {
            if (i % 2 == 0) sum += i;
        }
        System.out.println(sum);   /* 30 */

        /* while */
        int n = 1;
        while (n < 128) n *= 2;
        System.out.println(n);    /* 128 */

        /* 재귀 — 팩토리얼 */
        System.out.println(factorial(5));  /* 120 */
        System.out.println(factorial(10)); /* 3628800 */

        /* 피보나치 */
        System.out.println(fib(10));  /* 55 */

        System.out.println("Test03 PASSED");
    }
}
