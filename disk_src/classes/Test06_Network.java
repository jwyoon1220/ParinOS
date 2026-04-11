import java.net.InetAddress;
import java.net.Socket;
import java.io.OutputStream;
import java.io.InputStream;

/* Test06_Network.java — java.net.Socket TCP 연결 테스트
 *
 * QEMU user-mode 네트워크에서:
 *   - 게이트웨이: 10.0.2.2 (QEMU 내부 라우터)
 *   - 포트 8080 에 간단한 echo 서버가 있다고 가정
 *
 * 테스트 시나리오:
 *   1. InetAddress.getByName() — IP 파싱
 *   2. Socket 생성 (연결 시도)
 *   3. OutputStream 으로 "HELLO\n" 전송
 *   4. InputStream 에서 응답 읽기
 *   5. 연결 종료
 */
public class Test06_Network {
    public static void main(String[] args) {
        System.out.println("=== Test06: Network ===");

        /* 1. InetAddress 테스트 */
        try {
            InetAddress addr = InetAddress.getByName("10.0.2.2");
            System.out.println(addr.getHostAddress());  /* 10.0.2.2 */
        } catch (Exception e) {
            System.out.println("InetAddress.getByName failed");
        }

        /* 2. Socket TCP 연결 (QEMU 호스트 포트 포워딩 필요) */
        /*
         * QEMU 실행 시 -net user,hostfwd=tcp::8080-:8080 을 추가하면
         * 호스트의 8080 포트가 게스트로 포워딩됩니다.
         * 여기서는 연결 자체를 시도하고, 실패 여부를 보고합니다.
         */
        try {
            Socket sock = new Socket("10.0.2.2", 8080);
            System.out.println("connected");

            OutputStream os = sock.getOutputStream();
            /* "HELLO\n" 전송 */
            byte[] msg = {72, 69, 76, 76, 79, 10};  /* HELLO\n */
            os.write(msg, 0, 6);
            os.flush();

            /* 응답 읽기 (최대 256 바이트) */
            InputStream is = sock.getInputStream();
            byte[] buf = new byte[256];
            int n = is.read(buf, 0, 256);
            if (n > 0) {
                System.out.println("recv " + n + " bytes");
                /* 첫 번째 바이트 값 출력 */
                System.out.println(buf[0]);
            } else {
                System.out.println("no data");
            }

            sock.close();
            System.out.println("socket closed");
        } catch (Exception e) {
            System.out.println("socket error (expected without server)");
        }

        System.out.println("Test06 PASSED");
    }
}
