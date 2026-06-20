/*
 * user/curl/src/curl.c — Simple curl client using socket API
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "socket.h"

int main(int argc, const char **argv) {
    if (argc < 2) {
        printf("사용법: curl <URL>\n");
        printf("예시: curl http://143.248.2.2/ (KAIST IP)\n");
        return 1;
    }

    const char *url = argv[1];
    const char *host_start = url;
    if (strncmp(url, "http://", 7) == 0) {
        host_start = url + 7;
    }

    char host[128];
    int port = 80;
    const char *path = "/";

    int i = 0;
    while (host_start[i] && host_start[i] != '/' && host_start[i] != ':') {
        if (i < 127) {
            host[i] = host_start[i];
        }
        i++;
    }
    host[i] = '\0';

    const char *p = host_start + i;
    if (*p == ':') {
        p++;
        port = 0;
        while (*p >= '0' && *p <= '9') {
            port = port * 10 + (*p - '0');
            p++;
        }
    }
    if (*p == '/') {
        path = p;
    }

    uint32_t ip = 0;
    if (gethostbyname(host, &ip) != 0) {
        printf("호스트 IP 분석 실패 (IP 주소를 직접 입력해보세요): %s\n", host);
        return 1;
    }

    int sfd = socket(AF_INET, SOCK_STREAM, 0);
    if (sfd < 0) {
        printf("소켓 생성 실패: %d\n", sfd);
        return 1;
    }

    int ret = connect(sfd, ip, (uint16_t)port);
    if (ret < 0) {
        printf("연결 실패 (IP: %d.%d.%d.%d, Port: %d): %d\n",
               (int)(ip >> 24) & 0xFF,
               (int)(ip >> 16) & 0xFF,
               (int)(ip >> 8) & 0xFF,
               (int)ip & 0xFF,
               port, ret);
        closesocket(sfd);
        return 1;
    }

    char req[512];
    int req_len = snprintf(req, sizeof(req),
                           "GET %s HTTP/1.1\r\n"
                           "Host: %s\r\n"
                           "Connection: close\r\n"
                           "\r\n",
                           path, host);

    send(sfd, req, req_len);

    char buf[1024];
    int n;
    while ((n = recv(sfd, buf, sizeof(buf) - 1)) > 0) {
        buf[n] = '\0';
        printf("%s", buf);
    }

    closesocket(sfd);
    return 0;
}
