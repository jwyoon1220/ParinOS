/* Test05_StaticMethods.java — static 메서드 / Math */
public class Test05_StaticMethods {

    static int max3(int a, int b, int c) {
        int m = a > b ? a : b;
        return m > c ? m : c;
    }

    static int gcd(int a, int b) {
        while (b != 0) {
            int t = b;
            b = a % b;
            a = t;
        }
        return a;
    }

    static boolean isPrime(int n) {
        if (n < 2) return false;
        for (int i = 2; i * i <= n; i++) {
            if (n % i == 0) return false;
        }
        return true;
    }

    public static void main(String[] args) {
        System.out.println("=== Test05: Static Methods ===");

        System.out.println(max3(3, 7, 5));    /* 7 */
        System.out.println(gcd(48, 18));       /* 6 */

        /* 소수 출력 2..20 */
        for (int i = 2; i <= 20; i++) {
            if (isPrime(i)) {
                System.out.println(i);
            }
        }
        /* expected: 2 3 5 7 11 13 17 19 */

        System.out.println("Test05 PASSED");
    }
}
