/*
 * src/net/lwip_port.c — ParinOS 내장 경량 TCP/IP 스택
 *
 * 이 파일은 lwIP 없이도 동작하는 최소 구현입니다.
 * ARP, IPv4, TCP(클라이언트 기능), UDP 를 지원합니다.
 *
 * 실제 lwIP 포팅 시 이 파일을 lwIP src/ 연결로 교체하면 됩니다.
 * (src/net/net.c 의 ksocket_* 함수들이 이 파일의 lwip_tcp_* 를 호출합니다.)
 */

#include "lwip_port.h"
#include "net.h"
#include "../drivers/ne2000.h"
#include "../hal/vga.h"

/* ──────────────────────────────────────────────────────────────────
 * 네트워크 스택 상태
 * ────────────────────────────────────────────────────────────────── */

static uint32_t g_my_ip   = 0;   /* host byte order */
static uint32_t g_netmask = 0;
static uint32_t g_gateway = 0;
static uint8_t  g_mac[6]  = {0};
static int      g_net_up  = 0;

/* ── ARP 캐시 ────────────────────────────────────────────────────── */
#define ARP_CACHE_SIZE 16
typedef struct {
    uint32_t ip;
    uint8_t  mac[6];
    int      valid;
} arp_entry_t;
static arp_entry_t g_arp_cache[ARP_CACHE_SIZE];

/* ── TCP 소켓 수신 링 버퍼 ───────────────────────────────────────── */
#define TCP_RXBUF_SIZE 4096
typedef struct {
    uint8_t  buf[TCP_RXBUF_SIZE];
    uint32_t head, tail;
    int      connected;
    uint32_t remote_ip;
    uint16_t remote_port;
    uint16_t local_port;
    uint32_t seq_tx;
    uint32_t seq_rx;
    uint8_t  remote_mac[6];
} tcp_sock_t;

/* 소켓 테이블 (KSOCK_MAX 소켓) */
#define LWIP_SOCK_MAX 8
static tcp_sock_t g_tcp[LWIP_SOCK_MAX];

/* ── UDP 수신 버퍼 (단발 쿼리/응답용) ───────────────────────────── */
#define UDP_RXBUF_SIZE 512
static uint8_t  g_udp_rx_buf[UDP_RXBUF_SIZE];
static uint16_t g_udp_rx_len    = 0;   /* 0 = 비어있음 */
static uint16_t g_udp_rx_port   = 0;   /* 수신을 기다리는 로컬 포트 */

/* ── UDP 패킷 처리 ───────────────────────────────────────────────── */
static void handle_udp(uint32_t src_ip, const uint8_t *data, uint16_t len) {
    (void)src_ip;
    if (len < 8) return;
    uint16_t dst_port = (uint16_t)((data[2] << 8) | data[3]);
    uint16_t udp_len  = (uint16_t)((data[4] << 8) | data[5]);
    if (udp_len < 8 || udp_len > len) return;
    uint16_t payload_len = (uint16_t)(udp_len - 8);

    /* 등록된 로컬 포트로 온 패킷만 버퍼링 */
    if (g_udp_rx_port != 0 && dst_port == g_udp_rx_port && g_udp_rx_len == 0) {
        if (payload_len > UDP_RXBUF_SIZE)
            payload_len = UDP_RXBUF_SIZE;
        for (uint16_t i = 0; i < payload_len; i++)
            g_udp_rx_buf[i] = data[8 + i];
        g_udp_rx_len = payload_len;
    }
}


static uint16_t ip_checksum(const void *data, int len) {
    const uint16_t *p = (const uint16_t *)data;
    uint32_t sum = 0;
    while (len > 1) { sum += *p++; len -= 2; }
    if (len) sum += *(const uint8_t *)p;
    while (sum >> 16) sum = (sum & 0xFFFF) + (sum >> 16);
    return (uint16_t)~sum;
}

static uint16_t tcp_checksum(uint32_t src_ip, uint32_t dst_ip,
                              const void *tcphdr, uint16_t len) {
    /* pseudo header — 바이트 배열로 직접 처리하여 alignment 경고 회피 */
    uint8_t ph[12];
    ph[0]  = (uint8_t)(src_ip >> 24); ph[1]  = (uint8_t)(src_ip >> 16);
    ph[2]  = (uint8_t)(src_ip >>  8); ph[3]  = (uint8_t)(src_ip);
    ph[4]  = (uint8_t)(dst_ip >> 24); ph[5]  = (uint8_t)(dst_ip >> 16);
    ph[6]  = (uint8_t)(dst_ip >>  8); ph[7]  = (uint8_t)(dst_ip);
    ph[8]  = 0;
    ph[9]  = IP_PROTO_TCP;
    ph[10] = (uint8_t)(len >> 8);
    ph[11] = (uint8_t)(len);

    uint32_t sum = 0;
    for (int i = 0; i < 12; i += 2)
        sum += (uint32_t)((ph[i] << 8) | ph[i+1]);
    /* TCP segment */
    const uint8_t *tp = (const uint8_t *)tcphdr;
    uint16_t l = len;
    while (l > 1) { sum += (uint32_t)((tp[0] << 8) | tp[1]); tp += 2; l = (uint16_t)(l - 2); }
    if (l) sum += (uint32_t)(tp[0] << 8);
    while (sum >> 16) sum = (sum & 0xFFFF) + (sum >> 16);
    return (uint16_t)~sum;
}

/* ── htons / ntohl 헬퍼 (커널 freestanding) ────────────────────── */
static inline uint16_t ks_htons(uint16_t x) {
    return (uint16_t)((x >> 8) | (x << 8));
}
static inline uint32_t ks_htonl(uint32_t x) {
    return ((x & 0xff)       << 24) | ((x & 0xff00)     <<  8)
         | ((x & 0xff0000)   >>  8) | ((x & 0xff000000) >> 24);
}
#define ks_ntohs(x)  ks_htons(x)
#define ks_ntohl(x)  ks_htonl(x)

/* ── 이더넷 프레임 전송 ─────────────────────────────────────────── */
static int eth_send(const uint8_t *dst_mac, uint16_t ethertype,
                     const void *payload, uint16_t plen) {
    static uint8_t frame[1514];
    if (plen + 14 > 1514) return -1;

    /* 이더넷 헤더 */
    for (int i = 0; i < 6; i++) frame[i]     = dst_mac[i];
    for (int i = 0; i < 6; i++) frame[6 + i] = g_mac[i];
    frame[12] = (uint8_t)(ethertype >> 8);
    frame[13] = (uint8_t)(ethertype & 0xFF);

    for (uint16_t i = 0; i < plen; i++) frame[14 + i] = ((const uint8_t *)payload)[i];
    return ne2000_send(frame, (uint16_t)(14 + plen));
}

/* ── ARP 캐시 조회 / ARP Request 전송 ──────────────────────────── */
static int arp_cache_lookup(uint32_t ip, uint8_t *mac_out) {
    for (int i = 0; i < ARP_CACHE_SIZE; i++) {
        if (g_arp_cache[i].valid && g_arp_cache[i].ip == ip) {
            for (int j = 0; j < 6; j++) mac_out[j] = g_arp_cache[i].mac[j];
            return 1;
        }
    }
    return 0;
}

static void arp_cache_insert(uint32_t ip, const uint8_t *mac) {
    /* 기존 항목 갱신 */
    for (int i = 0; i < ARP_CACHE_SIZE; i++) {
        if (g_arp_cache[i].ip == ip) {
            g_arp_cache[i].valid = 1;
            for (int j = 0; j < 6; j++) g_arp_cache[i].mac[j] = mac[j];
            return;
        }
    }
    /* 빈 슬롯 */
    for (int i = 0; i < ARP_CACHE_SIZE; i++) {
        if (!g_arp_cache[i].valid) {
            g_arp_cache[i].valid = 1;
            g_arp_cache[i].ip = ip;
            for (int j = 0; j < 6; j++) g_arp_cache[i].mac[j] = mac[j];
            return;
        }
    }
    /* 덮어쓰기 (LRU 없이 슬롯 0 재사용) */
    g_arp_cache[0].valid = 1;
    g_arp_cache[0].ip = ip;
    for (int j = 0; j < 6; j++) g_arp_cache[0].mac[j] = mac[j];
}

static void arp_send_request(uint32_t target_ip) {
    arp_pkt_t arp;
    arp.htype = ks_htons(1);
    arp.ptype = ks_htons(0x0800);
    arp.hlen  = 6;
    arp.plen  = 4;
    arp.oper  = ks_htons(1);
    for (int i = 0; i < 6; i++) arp.sha[i] = g_mac[i];
    arp.spa   = ks_htonl(g_my_ip);
    for (int i = 0; i < 6; i++) arp.tha[i] = 0;
    arp.tpa   = ks_htonl(target_ip);
    static const uint8_t bcast[6] = {0xFF,0xFF,0xFF,0xFF,0xFF,0xFF};
    eth_send(bcast, ETHERTYPE_ARP, &arp, sizeof(arp));
}

/* ── IPv4 패킷 전송 ─────────────────────────────────────────────── */
static uint16_t g_ip_id = 0x1234;

static int ip_send(uint32_t dst_ip, uint8_t proto,
                    const void *payload, uint16_t plen) {
    uint8_t dst_mac[6];
    uint32_t next_hop = dst_ip;

    /* 다른 서브넷이면 게이트웨이 경유 */
    if ((dst_ip & g_netmask) != (g_my_ip & g_netmask))
        next_hop = g_gateway;

    if (!arp_cache_lookup(next_hop, dst_mac)) {
        arp_send_request(next_hop);
        /* ARP Reply 대기 (간단한 스핀 — 타이머 틱에서 개선 가능) */
        for (int i = 0; i < 100; i++) {
            ne2000_poll();
            if (arp_cache_lookup(next_hop, dst_mac)) break;
        }
        if (!arp_cache_lookup(next_hop, dst_mac)) {
            klog_warn("[lwip] ARP miss for %d.%d.%d.%d\n",
                      (next_hop>>24)&0xff, (next_hop>>16)&0xff,
                      (next_hop>>8)&0xff,   next_hop&0xff);
            return -1;
        }
    }

    /* IPv4 헤더 조립 */
    static uint8_t buf[1500];
    ipv4_hdr_t *iph = (ipv4_hdr_t *)buf;
    iph->ver_ihl   = 0x45;
    iph->dscp      = 0;
    iph->total_len = ks_htons((uint16_t)(20 + plen));
    iph->id        = ks_htons(g_ip_id++);
    iph->frag_off  = 0;
    iph->ttl       = 64;
    iph->protocol  = proto;
    iph->checksum  = 0;
    iph->src_ip    = ks_htonl(g_my_ip);
    iph->dst_ip    = ks_htonl(dst_ip);
    iph->checksum  = ip_checksum(iph, 20);

    for (uint16_t i = 0; i < plen; i++) buf[20 + i] = ((const uint8_t*)payload)[i];
    return eth_send(dst_mac, ETHERTYPE_IP, buf, (uint16_t)(20 + plen));
}

/* ── TCP 송신 ────────────────────────────────────────────────────── */
static int tcp_send_flags(int idx, uint8_t flags,
                           const void *data, uint16_t dlen) {
    tcp_sock_t *s = &g_tcp[idx];
    static uint8_t  seg[1460 + 20];
    tcp_hdr_t *th = (tcp_hdr_t *)seg;
    th->src_port = ks_htons(s->local_port);
    th->dst_port = ks_htons(s->remote_port);
    th->seq      = ks_htonl(s->seq_tx);
    th->ack      = ks_htonl(s->seq_rx);
    th->data_off = 0x50;  /* 5×4=20 bytes header, no options */
    th->flags    = flags;
    th->window   = ks_htons(4096);
    th->checksum = 0;
    th->urgent   = 0;

    if (dlen > 0 && data)
        for (uint16_t i = 0; i < dlen; i++) seg[20 + i] = ((const uint8_t*)data)[i];

    uint16_t tlen = (uint16_t)(20 + dlen);
    th->checksum = tcp_checksum(ks_htonl(g_my_ip),
                                 ks_htonl(s->remote_ip),
                                 seg, tlen);
    if (flags & (TCP_SYN|TCP_FIN)) s->seq_tx++;
    if (dlen > 0) s->seq_tx += dlen;
    return ip_send(s->remote_ip, IP_PROTO_TCP, seg, tlen);
}

/* ── ARP 패킷 처리 ───────────────────────────────────────────────── */
static void handle_arp(const uint8_t *data, uint16_t len) {
    if (len < (uint16_t)sizeof(arp_pkt_t)) return;
    const arp_pkt_t *arp = (const arp_pkt_t *)data;

    uint32_t spa = ks_ntohl(arp->spa);
    uint32_t tpa = ks_ntohl(arp->tpa);

    arp_cache_insert(spa, arp->sha);

    if (ks_ntohs(arp->oper) == 1 && tpa == g_my_ip) {
        /* ARP Request 에 Reply 전송 */
        arp_pkt_t rep;
        rep.htype = ks_htons(1);
        rep.ptype = ks_htons(0x0800);
        rep.hlen  = 6;
        rep.plen  = 4;
        rep.oper  = ks_htons(2);
        for (int i = 0; i < 6; i++) rep.sha[i] = g_mac[i];
        rep.spa   = ks_htonl(g_my_ip);
        for (int i = 0; i < 6; i++) rep.tha[i] = arp->sha[i];
        rep.tpa   = arp->spa;
        eth_send(arp->sha, ETHERTYPE_ARP, &rep, sizeof(rep));
    }
}

/* ── TCP 패킷 처리 ───────────────────────────────────────────────── */
static void handle_tcp(uint32_t src_ip, const uint8_t *data, uint16_t len) {
    if (len < 20) return;
    const tcp_hdr_t *th = (const tcp_hdr_t *)data;
    uint16_t dst_port = ks_ntohs(th->dst_port);
    uint16_t src_port = ks_ntohs(th->src_port);
    uint8_t  flags    = th->flags;
    uint32_t seq      = ks_ntohl(th->seq);
    uint32_t ack      = ks_ntohl(th->ack);
    uint8_t  hdr_len  = (uint8_t)((th->data_off >> 4) * 4);

    /* 해당 소켓 탐색 */
    for (int i = 0; i < LWIP_SOCK_MAX; i++) {
        tcp_sock_t *s = &g_tcp[i];
        if (!s->connected) continue;
        if (s->remote_ip != src_ip) continue;
        if (s->remote_port != src_port) continue;
        if (s->local_port != dst_port) continue;

        (void)ack;

        if (flags & TCP_RST) {
            s->connected = 0;
            return;
        }
        if (flags & TCP_FIN) {
            s->seq_rx = seq + 1;
            tcp_send_flags(i, TCP_ACK | TCP_FIN, (void*)0, 0);
            s->connected = 0;
            return;
        }
        if (flags & TCP_ACK) {
            /* SYN-ACK 처리 (3-way handshake 마지막) */
            if (flags & TCP_SYN) {
                /* SYN-ACK 수신 → ACK 전송 */
                s->seq_rx = seq + 1;
                tcp_send_flags(i, TCP_ACK, (void*)0, 0);
                return;
            }
        }
        /* 페이로드 처리 */
        uint16_t payload_len = (uint16_t)(len - hdr_len);
        if (payload_len > 0) {
            const uint8_t *payload = data + hdr_len;
            /* 수신 링 버퍼에 저장 */
            for (uint16_t j = 0; j < payload_len; j++) {
                uint32_t next = (s->tail + 1) % TCP_RXBUF_SIZE;
                if (next != s->head) {
                    s->buf[s->tail] = payload[j];
                    s->tail = next;
                }
            }
            s->seq_rx = seq + payload_len;
            tcp_send_flags(i, TCP_ACK, (void*)0, 0);
        }
        return;
    }
}

/* ── ICMP Echo Reply ─────────────────────────────────────────────── */
static void handle_icmp(uint32_t src_ip, const uint8_t *data, uint16_t len) {
    if (len < 8) return;
    if (len > 1500) return;    /* 과도한 패킷 무시 */
    if (data[0] != 8) return;  /* type=8: Echo Request */
    /* Echo Reply */
    static uint8_t reply[1500];
    for (uint16_t i = 0; i < len; i++) reply[i] = data[i];
    reply[0] = 0;  /* type=0: Echo Reply */
    reply[2] = 0; reply[3] = 0;
    uint16_t csum = ip_checksum(reply, len);
    reply[2] = (uint8_t)(csum >> 8);
    reply[3] = (uint8_t)(csum & 0xFF);
    ip_send(src_ip, IP_PROTO_ICMP, reply, len);
}

/* ── IPv4 패킷 처리 ─────────────────────────────────────────────── */
static void handle_ip(const uint8_t *data, uint16_t len) {
    if (len < 20) return;
    const ipv4_hdr_t *iph = (const ipv4_hdr_t *)data;
    if ((iph->ver_ihl >> 4) != 4) return;
    uint8_t  hlen    = (uint8_t)((iph->ver_ihl & 0x0F) * 4);
    uint16_t tot_len = ks_ntohs(iph->total_len);
    uint32_t dst_ip  = ks_ntohl(iph->dst_ip);
    uint32_t src_ip  = ks_ntohl(iph->src_ip);

    /* 내 IP 또는 브로드캐스트만 처리 */
    if (dst_ip != g_my_ip && dst_ip != 0xFFFFFFFFU) return;
    if (tot_len > len || hlen > tot_len) return;

    const uint8_t *payload = data + hlen;
    uint16_t       plen    = (uint16_t)(tot_len - hlen);

    switch (iph->protocol) {
        case IP_PROTO_ICMP: handle_icmp(src_ip, payload, plen); break;
        case IP_PROTO_TCP:  handle_tcp(src_ip, payload, plen);  break;
        case IP_PROTO_UDP:  handle_udp(src_ip, payload, plen);  break;
        default: break;
    }
}

/* ════════════════════════════════════════════════════════════════════
 * 공개 API
 * ════════════════════════════════════════════════════════════════════ */

void lwip_init(uint32_t my_ip, uint32_t mask, uint32_t gw) {
    g_my_ip   = my_ip;
    g_netmask = mask;
    g_gateway = gw;
    for (int i = 0; i < ARP_CACHE_SIZE; i++) g_arp_cache[i].valid = 0;
    for (int i = 0; i < LWIP_SOCK_MAX;  i++) g_tcp[i].connected = 0;
    ne2000_get_mac(g_mac);
    g_net_up = 1;
    klog_info("[lwip] stack up — IP %d.%d.%d.%d  MAC %02x:%02x:%02x:%02x:%02x:%02x\n",
              (my_ip>>24)&0xff, (my_ip>>16)&0xff,
              (my_ip>>8)&0xff,   my_ip&0xff,
              g_mac[0], g_mac[1], g_mac[2],
              g_mac[3], g_mac[4], g_mac[5]);
}

void lwip_rx_input(const uint8_t *pkt, uint16_t len) {
    if (len < 14) return;
    const eth_hdr_t *eth = (const eth_hdr_t *)pkt;
    uint16_t etype = (uint16_t)((eth->ethertype >> 8) | (eth->ethertype << 8));
    switch (etype) {
        case ETHERTYPE_ARP: handle_arp(pkt + 14, (uint16_t)(len - 14)); break;
        case ETHERTYPE_IP:  handle_ip (pkt + 14, (uint16_t)(len - 14)); break;
        default: break;
    }
}

void lwip_poll(void) {
    if (g_net_up) ne2000_poll();
}

int lwip_tcp_connect(int idx, uint32_t dst_ip, uint16_t dst_port) {
    if (idx < 0 || idx >= LWIP_SOCK_MAX) return -1;
    tcp_sock_t *s = &g_tcp[idx];
    s->remote_ip   = dst_ip;
    s->remote_port = dst_port;
    s->local_port  = (uint16_t)(1024 + idx * 10 + (dst_port & 0xF));
    s->seq_tx      = 0xABCD1234U + (uint32_t)(idx * 0x1000);
    s->seq_rx      = 0;
    s->head        = 0;
    s->tail        = 0;
    s->connected   = 1;

    /* SYN 전송 */
    tcp_send_flags(idx, TCP_SYN, (void*)0, 0);

    /* SYN-ACK 대기 (최대 200 폴링) */
    for (int i = 0; i < 200; i++) {
        ne2000_poll();
        if (s->seq_rx != 0) return 0;   /* SYN-ACK 수신됨 */
    }
    s->connected = 0;
    return -1;   /* 연결 실패 */
}

int lwip_tcp_send(int idx, const void *data, uint32_t len) {
    if (idx < 0 || idx >= LWIP_SOCK_MAX) return -1;
    if (!g_tcp[idx].connected) return -1;
    if (len > 1440) len = 1440;
    return tcp_send_flags(idx, TCP_PSH | TCP_ACK, data, (uint16_t)len);
}

int lwip_tcp_recv(int idx, void *buf, uint32_t len) {
    if (idx < 0 || idx >= LWIP_SOCK_MAX) return -1;
    tcp_sock_t *s = &g_tcp[idx];
    if (!s->connected && s->head == s->tail) return -1;
    uint32_t n = 0;
    uint8_t *out = (uint8_t *)buf;
    while (n < len && s->head != s->tail) {
        out[n++] = s->buf[s->head];
        s->head  = (s->head + 1) % TCP_RXBUF_SIZE;
    }
    return (int)n;
}

void lwip_tcp_close(int idx) {
    if (idx < 0 || idx >= LWIP_SOCK_MAX) return;
    if (g_tcp[idx].connected)
        tcp_send_flags(idx, TCP_FIN | TCP_ACK, (void*)0, 0);
    g_tcp[idx].connected = 0;
}

uint32_t lwip_get_ip(void) { return g_my_ip; }

int lwip_arp_lookup(uint32_t ip, uint8_t *mac_out) {
    return arp_cache_lookup(ip, mac_out);
}

/* ── UDP 단발 쿼리/응답 ──────────────────────────────────────────── */
int lwip_udp_query(uint32_t dst_ip, uint16_t dst_port, uint16_t src_port,
                   const void *tx_buf, uint16_t tx_len,
                   void *rx_buf, uint16_t rx_max,
                   int timeout_polls) {
    if (!g_net_up) return -1;

    /* UDP 헤더 + 페이로드 조립 */
    static uint8_t pkt[8 + 512];
    if (tx_len > 512) tx_len = 512;
    uint16_t total = (uint16_t)(8 + tx_len);

    pkt[0] = (uint8_t)(src_port >> 8);
    pkt[1] = (uint8_t)(src_port & 0xFF);
    pkt[2] = (uint8_t)(dst_port >> 8);
    pkt[3] = (uint8_t)(dst_port & 0xFF);
    pkt[4] = (uint8_t)(total >> 8);
    pkt[5] = (uint8_t)(total & 0xFF);
    pkt[6] = 0;   /* 체크섬 (선택적 — 0=미사용) */
    pkt[7] = 0;

    const uint8_t *d = (const uint8_t *)tx_buf;
    for (uint16_t i = 0; i < tx_len; i++) pkt[8 + i] = d[i];

    /* 수신 버퍼 등록 */
    g_udp_rx_port = src_port;
    g_udp_rx_len  = 0;

    if (ip_send(dst_ip, IP_PROTO_UDP, pkt, total) < 0) {
        g_udp_rx_port = 0;
        return -1;
    }

    /* 응답 대기 */
    for (int i = 0; i < timeout_polls; i++) {
        ne2000_poll();
        if (g_udp_rx_len > 0) {
            uint16_t n = g_udp_rx_len;
            if (n > rx_max) n = rx_max;
            uint8_t *out = (uint8_t *)rx_buf;
            for (uint16_t j = 0; j < n; j++) out[j] = g_udp_rx_buf[j];
            g_udp_rx_port = 0;
            g_udp_rx_len  = 0;
            return (int)n;
        }
    }

    g_udp_rx_port = 0;
    return -1;   /* 타임아웃 */
}

