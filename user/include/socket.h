/*
 * user/include/socket.h — ParinOS 유저 프로그램 POSIX 소켓 인터페이스
 *
 * 지원 도메인:  AF_INET (IPv4)
 * 지원 타입:    SOCK_STREAM (TCP), SOCK_DGRAM (UDP)
 *
 * 네트워크 바이트 오더(big-endian)와 호스트 바이트 오더(little-endian)를
 * 변환하는 htons/ntohs/htonl/ntohl 매크로 포함.
 *
 * 모든 함수는 syscall.h 의 syscall2/3 을 통해 커널 소켓 레이어를 호출합니다.
 */

#ifndef PARINOS_SOCKET_H
#define PARINOS_SOCKET_H

#include <stdint.h>
#include "syscall.h"

/* ── 주소/프로토콜 패밀리 ──────────────────────────────────────────── */
#define AF_INET      2

/* ── 소켓 타입 ────────────────────────────────────────────────────── */
#define SOCK_STREAM  1
#define SOCK_DGRAM   2

/* ── 특수 주소 ────────────────────────────────────────────────────── */
#define INADDR_ANY       0x00000000U   /* 0.0.0.0        */
#define INADDR_LOOPBACK  0x0100007fU   /* 127.0.0.1 (NBO)*/

/* ── 바이트 오더 변환 ─────────────────────────────────────────────── */
#define htons(x) ((uint16_t)(((uint16_t)(x) >> 8) | ((uint16_t)(x) << 8)))
#define ntohs(x) htons(x)
#define htonl(x) (                                     \
    (((uint32_t)(x) & 0x000000FFU) << 24) |           \
    (((uint32_t)(x) & 0x0000FF00U) <<  8) |           \
    (((uint32_t)(x) & 0x00FF0000U) >>  8) |           \
    (((uint32_t)(x) & 0xFF000000U) >> 24) )
#define ntohl(x) htonl(x)

/* ── IPv4 주소 구성 매크로 (NBO) ─────────────────────────────────── */
#define MAKE_IP(a,b,c,d) \
    ((uint32_t)(a) | ((uint32_t)(b)<<8) | \
     ((uint32_t)(c)<<16) | ((uint32_t)(d)<<24))

/* ── BSD 소켓 주소 구조체 ─────────────────────────────────────────── */
struct in_addr {
    uint32_t s_addr;   /* IPv4 주소 (network byte order) */
};

struct sockaddr_in {
    uint16_t       sin_family;   /* AF_INET */
    uint16_t       sin_port;     /* port (network byte order) */
    struct in_addr sin_addr;
    char           sin_zero[8];  /* 패딩 */
};

/* ── 소켓 인라인 래퍼 ─────────────────────────────────────────────── */

/**
 * socket(domain, type, protocol) — 소켓 파일 디스크립터 생성.
 * 성공 시 fd(>=16) 반환, 실패 시 음수 에러 코드.
 */
static inline int socket(int domain, int type, int protocol) {
    return syscall3(SYS_SOCKET, domain, type, protocol);
}

/**
 * connect(sfd, addr_nbo, port_hbo) — TCP 연결 시도.
 * addr: IPv4 주소 (network byte order)
 * port: 포트 번호 (host byte order)
 */
static inline int connect(int sfd, uint32_t addr, uint16_t port) {
    return syscall3(SYS_CONNECT, sfd, (int)addr, (int)port);
}

/**
 * bind(sfd, addr_nbo, port_hbo) — 로컬 주소 바인드.
 */
static inline int bind(int sfd, uint32_t addr, uint16_t port) {
    return syscall3(SYS_BIND, sfd, (int)addr, (int)port);
}

/**
 * listen(sfd, backlog) — 연결 수신 대기 설정.
 */
static inline int listen(int sfd, int backlog) {
    return syscall2(SYS_LISTEN, sfd, backlog);
}

/**
 * accept(sfd, &client_addr, &client_port) — 새 연결 수락.
 * client_addr, client_port 는 NULL 가능.
 * 성공 시 새 소켓 FD, 실패 시 음수 에러 코드.
 */
static inline int accept(int sfd, uint32_t *client_addr,
                          uint16_t *client_port) {
    return syscall3(SYS_ACCEPT, sfd, (int)client_addr, (int)client_port);
}

/**
 * send(sfd, buf, len) — 데이터 전송.
 * 성공 시 전송된 바이트 수, 실패 시 음수 에러 코드.
 */
static inline int send(int sfd, const void *buf, int len) {
    return syscall3(SYS_SEND, sfd, (int)buf, len);
}

/**
 * recv(sfd, buf, len) — 데이터 수신.
 * 성공 시 수신된 바이트 수, 0 = EOF, 실패 시 음수.
 */
static inline int recv(int sfd, void *buf, int len) {
    return syscall3(SYS_RECV, sfd, (int)buf, len);
}

/**
 * gethostbyname(name, &addr) — 호스트명을 IPv4 주소로 변환.
 * addr 는 network byte order 로 채워진다.
 * 성공 시 0, 실패 시 음수 에러 코드.
 */
static inline int gethostbyname(const char *name, uint32_t *addr) {
    return syscall2(SYS_GETHOST, (int)name, (int)addr);
}

/**
 * closesocket(sfd) — 소켓 닫기.
 */
static inline void closesocket(int sfd) {
    syscall1(SYS_CLOSE, sfd);
}

#endif /* PARINOS_SOCKET_H */
