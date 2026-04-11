/*
 * src/net/lwip_port.h — ParinOS lwIP 포팅 인터페이스
 *
 * lwIP을 커널 빌드에 직접 포함시키는 대신, 이 파일은 lwIP의 핵심 기능을
 * ParinOS 커널 네트워크 스택으로 직접 에뮬레이션합니다.
 *
 * 구현 전략:
 *   1단계 (현재): 내장 경량 TCP/IP 스택 — ARP + IP + TCP/UDP 기초 구현
 *                  실제 lwIP 포팅을 위한 완전한 glue 인터페이스 제공
 *   2단계 (나중): lwIP 소스를 src/lwip/ 에 배치하고 이 파일의 함수들을
 *                  lwIP API로 교체하면 됩니다.
 *
 * 네트워크 초기화 순서:
 *   1. ne2000_init(lwip_rx_input) — NIC 드라이버 초기화
 *   2. lwip_init()                — IP 스택 초기화
 *   3. scheduler_tick()에서 lwip_poll() 주기적 호출
 */

#ifndef PARINOS_LWIP_PORT_H
#define PARINOS_LWIP_PORT_H

#include <stdint.h>
#include <stddef.h>

/* ── IP/이더넷 구조체 ───────────────────────────────────────────── */

/* 이더넷 프레임 헤더 */
typedef struct __attribute__((packed)) {
    uint8_t  dst[6];
    uint8_t  src[6];
    uint16_t ethertype;
} eth_hdr_t;

/* ARP 패킷 (IPv4-Ethernet) */
typedef struct __attribute__((packed)) {
    uint16_t htype;   /* 1 = Ethernet */
    uint16_t ptype;   /* 0x0800 = IPv4 */
    uint8_t  hlen;    /* 6 */
    uint8_t  plen;    /* 4 */
    uint16_t oper;    /* 1=Request, 2=Reply */
    uint8_t  sha[6];  /* Sender MAC */
    uint32_t spa;     /* Sender IP */
    uint8_t  tha[6];  /* Target MAC */
    uint32_t tpa;     /* Target IP */
} arp_pkt_t;

/* IPv4 헤더 */
typedef struct __attribute__((packed)) {
    uint8_t  ver_ihl;
    uint8_t  dscp;
    uint16_t total_len;
    uint16_t id;
    uint16_t frag_off;
    uint8_t  ttl;
    uint8_t  protocol;
    uint16_t checksum;
    uint32_t src_ip;
    uint32_t dst_ip;
} ipv4_hdr_t;

/* TCP 헤더 */
typedef struct __attribute__((packed)) {
    uint16_t src_port;
    uint16_t dst_port;
    uint32_t seq;
    uint32_t ack;
    uint8_t  data_off;
    uint8_t  flags;
    uint16_t window;
    uint16_t checksum;
    uint16_t urgent;
} tcp_hdr_t;

/* UDP 헤더 */
typedef struct __attribute__((packed)) {
    uint16_t src_port;
    uint16_t dst_port;
    uint16_t length;
    uint16_t checksum;
} udp_hdr_t;

/* ── TCP 플래그 ──────────────────────────────────────────────────── */
#define TCP_FIN  0x01
#define TCP_SYN  0x02
#define TCP_RST  0x04
#define TCP_PSH  0x08
#define TCP_ACK  0x10
#define TCP_URG  0x20

/* ── 이더타입 ────────────────────────────────────────────────────── */
#define ETHERTYPE_ARP  0x0806
#define ETHERTYPE_IP   0x0800

/* ── IP 프로토콜 ─────────────────────────────────────────────────── */
#define IP_PROTO_ICMP  1
#define IP_PROTO_TCP   6
#define IP_PROTO_UDP   17

/* ── lwIP 포팅 공개 API ──────────────────────────────────────────── */

/**
 * 네트워크 스택 초기화.
 * ne2000_init() 이후 호출하세요.
 *
 * @param my_ip  호스트 IPv4 (host byte order, e.g. (192<<24)|(168<<16)|(1<<8)|10)
 * @param mask   서브넷 마스크
 * @param gw     게이트웨이
 */
void lwip_init(uint32_t my_ip, uint32_t mask, uint32_t gw);

/**
 * NIC 수신 콜백 — ne2000_init(lwip_rx_input) 에 전달하세요.
 * 이더넷 프레임을 받아 ARP/IP/TCP/UDP 처리를 수행합니다.
 */
void lwip_rx_input(const uint8_t *pkt, uint16_t len);

/**
 * 주기적 폴링 — scheduler_tick() 등에서 매 타이머 틱마다 호출.
 * NIC 수신 폴링, TCP 재전송 타이머, ARP 캐시 갱신 등을 처리합니다.
 */
void lwip_poll(void);

/**
 * TCP 연결 (소켓 레이어에서 호출).
 * @return 0 = 성공, -1 = 실패
 */
int lwip_tcp_connect(int sock_idx, uint32_t dst_ip, uint16_t dst_port);

/**
 * TCP 데이터 전송.
 */
int lwip_tcp_send(int sock_idx, const void *data, uint32_t len);

/**
 * TCP 수신 버퍼에서 데이터 읽기.
 * @return 바이트 수, 0 = 버퍼 비어있음, -1 = 연결 끊김
 */
int lwip_tcp_recv(int sock_idx, void *buf, uint32_t len);

/**
 * TCP 소켓 닫기.
 */
void lwip_tcp_close(int sock_idx);

/**
 * 현재 장치 IPv4 주소 반환 (host byte order).
 */
uint32_t lwip_get_ip(void);

/**
 * ARP 캐시에서 MAC 주소 조회.
 * @return 1 = 캐시 히트, 0 = 미스 (ARP Request 전송됨)
 */
int lwip_arp_lookup(uint32_t ip, uint8_t *mac_out);

#endif /* PARINOS_LWIP_PORT_H */
