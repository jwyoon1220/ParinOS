/*
 * src/net/net.c - ParinOS kernel socket layer
 *
 * This layer maps BSD-style socket FDs to the internal lwIP-based TCP/IP
 * stack implemented in lwip_port.c.  All socket I/O delegates to
 * lwip_tcp_connect / lwip_tcp_send / lwip_tcp_recv etc.
 */

#include "net.h"
#include "lwip_port.h"
#include "../hal/vga.h"
#include "../std/kstring.h"

/* ── Internal errno codes (Linux-compatible subset) ─────────────────── */
#define EBADF    9
#define ENFILE  23
#define EINVAL  22
#define EISCONN 106
#define ENOTCONN 107
#define ENONET  115

/* ── Socket table ────────────────────────────────────────────────────── */
static ksocket_t g_sockets[KSOCK_MAX];
static int       g_net_inited = 0;

static void net_init_once(void) {
    if (g_net_inited) return;
    for (int i = 0; i < KSOCK_MAX; i++) g_sockets[i].state = KSOCK_UNUSED;
    g_net_inited = 1;
}

static int sfd_to_idx(int sfd) {
    int idx = sfd - SOCK_FD_BASE;
    if (idx < 0 || idx >= KSOCK_MAX) return -1;
    return idx;
}

/* ── ksocket_create ───────────────────────────────────────────────────── */
int ksocket_create(int domain, int type) {
    net_init_once();
    if (domain != AF_INET) return -(EINVAL);
    if (type != SOCK_STREAM && type != SOCK_DGRAM) return -(EINVAL);
    for (int i = 0; i < KSOCK_MAX; i++) {
        if (g_sockets[i].state == KSOCK_UNUSED) {
            g_sockets[i].state       = KSOCK_CREATED;
            g_sockets[i].domain      = domain;
            g_sockets[i].type        = type;
            g_sockets[i].local_addr  = 0;
            g_sockets[i].local_port  = 0;
            g_sockets[i].remote_addr = 0;
            g_sockets[i].remote_port = 0;
            klog_info("[net] socket created: fd=%d\n", SOCK_FD_BASE + i);
            return SOCK_FD_BASE + i;
        }
    }
    return -(ENFILE);
}

/* ── ksocket_bind ─────────────────────────────────────────────────────── */
int ksocket_bind(int sfd, uint32_t addr, uint16_t port) {
    int idx = sfd_to_idx(sfd);
    if (idx < 0 || g_sockets[idx].state == KSOCK_UNUSED) return -(EBADF);
    g_sockets[idx].local_addr = addr;
    g_sockets[idx].local_port = port;
    g_sockets[idx].state      = KSOCK_BOUND;
    klog_info("[net] bind: fd=%d port=%u\n", sfd, (unsigned)port);
    return 0;
}

/* ── ksocket_listen ───────────────────────────────────────────────────── */
int ksocket_listen(int sfd, int backlog) {
    (void)backlog;
    int idx = sfd_to_idx(sfd);
    if (idx < 0 || g_sockets[idx].state == KSOCK_UNUSED) return -(EBADF);
    g_sockets[idx].state = KSOCK_LISTENING;
    klog_info("[net] listen: fd=%d\n", sfd);
    return 0;
}

/* ── ksocket_accept ───────────────────────────────────────────────────── */
int ksocket_accept(int sfd, uint32_t *client_addr, uint16_t *client_port) {
    int idx = sfd_to_idx(sfd);
    if (idx < 0 || g_sockets[idx].state != KSOCK_LISTENING) return -(EBADF);
    if (client_addr)  *client_addr  = 0;
    if (client_port)  *client_port  = 0;
    /* Server-side accept not yet implemented in lwip_port */
    return -(ENONET);
}

/* ── ksocket_connect ──────────────────────────────────────────────────── */
int ksocket_connect(int sfd, uint32_t addr, uint16_t port) {
    int idx = sfd_to_idx(sfd);
    if (idx < 0 || g_sockets[idx].state == KSOCK_UNUSED) return -(EBADF);
    if (g_sockets[idx].state == KSOCK_CONNECTED) return -(EISCONN);

    g_sockets[idx].remote_addr = addr;
    g_sockets[idx].remote_port = port;

    klog_info("[net] connecting fd=%d -> %u.%u.%u.%u:%u\n",
              sfd,
              (addr >> 24) & 0xff, (addr >> 16) & 0xff,
              (addr >>  8) & 0xff,  addr        & 0xff,
              (unsigned)port);

    /* Delegate to lwIP TCP stack */
    int ret = lwip_tcp_connect(idx, addr, port);
    if (ret == 0) {
        g_sockets[idx].state = KSOCK_CONNECTED;
        klog_info("[net] connected fd=%d\n", sfd);
        return 0;
    }
    klog_warn("[net] connect failed fd=%d\n", sfd);
    return -(ENONET);
}

/* ── ksocket_send ─────────────────────────────────────────────────────── */
int ksocket_send(int sfd, const void *buf, uint32_t len) {
    int idx = sfd_to_idx(sfd);
    if (idx < 0 || g_sockets[idx].state == KSOCK_UNUSED) return -(EBADF);
    if (g_sockets[idx].state != KSOCK_CONNECTED)          return -(ENOTCONN);
    return lwip_tcp_send(idx, buf, len);
}

/* ── ksocket_recv ─────────────────────────────────────────────────────── */
int ksocket_recv(int sfd, void *buf, uint32_t len) {
    int idx = sfd_to_idx(sfd);
    if (idx < 0 || g_sockets[idx].state == KSOCK_UNUSED) return -(EBADF);
    if (g_sockets[idx].state != KSOCK_CONNECTED)          return -(ENOTCONN);
    /* Poll NIC first to drain any pending data */
    lwip_poll();
    return lwip_tcp_recv(idx, buf, len);
}

/* ── ksocket_close ────────────────────────────────────────────────────── */
int ksocket_close(int sfd) {
    int idx = sfd_to_idx(sfd);
    if (idx < 0) return -(EBADF);
    if (g_sockets[idx].state == KSOCK_UNUSED) return -(EBADF);
    if (g_sockets[idx].state == KSOCK_CONNECTED)
        lwip_tcp_close(idx);
    g_sockets[idx].state = KSOCK_UNUSED;
    klog_info("[net] socket closed: fd=%d\n", sfd);
    return 0;
}

/* ── ksocket_gethostbyname ────────────────────────────────────────────── */
int ksocket_gethostbyname(const char *name, uint32_t *addr_out) {
    if (!name || !addr_out) return -(EINVAL);

    /* Loopback shortcut */
    if (strcmp(name, "localhost") == 0 ||
        strcmp(name, "127.0.0.1") == 0) {
        /* Return loopback as own IP so we can actually connect via NIC */
        *addr_out = lwip_get_ip();
        if (*addr_out == 0) *addr_out = 0x7f000001U; /* 127.0.0.1 fallback */
        return 0;
    }

    /*
     * Dotted-decimal quick parse (e.g. "192.168.1.1")
     * For full DNS support, replace with lwIP dns_gethostbyname().
     */
    uint32_t a = 0, b = 0, c = 0, d = 0;
    int matched = 0;
    const char *p = name;
    /* Manual sscanf-style parse (no stdlib in kernel) */
    a = 0; while (*p >= '0' && *p <= '9') { a = a*10 + (uint32_t)(*p++ - '0'); }
    if (*p == '.') {
        p++;
        b = 0; while (*p >= '0' && *p <= '9') { b = b*10 + (uint32_t)(*p++ - '0'); }
        if (*p == '.') {
            p++;
            c = 0; while (*p >= '0' && *p <= '9') { c = c*10 + (uint32_t)(*p++ - '0'); }
            if (*p == '.') {
                p++;
                d = 0; while (*p >= '0' && *p <= '9') { d = d*10 + (uint32_t)(*p++ - '0'); }
                if (*p == '\0') matched = 1;
            }
        }
    }
    if (matched) {
        *addr_out = (a << 24) | (b << 16) | (c << 8) | d;
        return 0;
    }

    klog_info("[net] gethostbyname('%s') -> no DNS resolver\n", name);
    *addr_out = 0;
    return -(ENONET);
}

