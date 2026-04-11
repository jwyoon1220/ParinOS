/*
 * src/net/net.c - ParinOS kernel socket layer (stub implementation)
 *
 * All operations are stored in a small socket table. Actual network I/O
 * (send/recv) requires a real network stack (e.g. lwIP + NIC driver).
 * Until that is wired up these calls return -ENONET (-115).
 *
 * Replacing this file with lwIP glue code is the only change needed to
 * make the full Java networking stack functional.
 */

#include "net.h"
#include "../hal/vga.h"
#include "../std/kstring.h"

/* ── Internal errno codes (Linux-compatible subset) ─────────────────── */
#define EBADF    9
#define ENFILE  23
#define EINVAL  22
#define EISCONN 106
#define ENOTCONN 107
#define ENONET  115   /* machine is not on the network */

/* ── Socket table ────────────────────────────────────────────────────── */
static ksocket_t g_sockets[KSOCK_MAX];
static int       g_net_inited = 0;

static void net_init_once(void) {
    if (g_net_inited) return;
    for (int i = 0; i < KSOCK_MAX; i++) g_sockets[i].state = KSOCK_UNUSED;
    g_net_inited = 1;
}

/* Translate a socket FD to a table index (or -1). */
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
    /* No real network stack: listening is recorded but no connections arrive. */
    return 0;
}

/* ── ksocket_accept ───────────────────────────────────────────────────── */
int ksocket_accept(int sfd, uint32_t *client_addr, uint16_t *client_port) {
    int idx = sfd_to_idx(sfd);
    if (idx < 0 || g_sockets[idx].state != KSOCK_LISTENING) return -(EBADF);

    if (client_addr)  *client_addr  = 0;
    if (client_port)  *client_port  = 0;

    /* Stub: no real connections → block-free but signals "no connection yet". */
    klog_info("[net] accept stub: fd=%d -> ENONET\n", sfd);
    return -(ENONET);
}

/* ── ksocket_connect ──────────────────────────────────────────────────── */
int ksocket_connect(int sfd, uint32_t addr, uint16_t port) {
    int idx = sfd_to_idx(sfd);
    if (idx < 0 || g_sockets[idx].state == KSOCK_UNUSED) return -(EBADF);
    if (g_sockets[idx].state == KSOCK_CONNECTED) return -(EISCONN);

    g_sockets[idx].remote_addr = addr;
    g_sockets[idx].remote_port = port;
    g_sockets[idx].state       = KSOCK_CONNECTED;

    klog_info("[net] connect stub: fd=%d -> %u.%u.%u.%u:%u (no stack, ENONET)\n",
              sfd,
              (addr)        & 0xff,
              (addr >> 8)   & 0xff,
              (addr >> 16)  & 0xff,
              (addr >> 24)  & 0xff,
              (unsigned)port);
    /*
     * Return -ENONET because there is no actual TCP/IP stack.
     * When lwIP is wired up, replace this with the real connect call.
     */
    return -(ENONET);
}

/* ── ksocket_send ─────────────────────────────────────────────────────── */
int ksocket_send(int sfd, const void *buf, uint32_t len) {
    (void)buf; (void)len;
    int idx = sfd_to_idx(sfd);
    if (idx < 0 || g_sockets[idx].state == KSOCK_UNUSED) return -(EBADF);
    if (g_sockets[idx].state != KSOCK_CONNECTED)          return -(ENOTCONN);
    return -(ENONET);
}

/* ── ksocket_recv ─────────────────────────────────────────────────────── */
int ksocket_recv(int sfd, void *buf, uint32_t len) {
    (void)buf; (void)len;
    int idx = sfd_to_idx(sfd);
    if (idx < 0 || g_sockets[idx].state == KSOCK_UNUSED) return -(EBADF);
    if (g_sockets[idx].state != KSOCK_CONNECTED)          return -(ENOTCONN);
    return -(ENONET);
}

/* ── ksocket_close ────────────────────────────────────────────────────── */
int ksocket_close(int sfd) {
    int idx = sfd_to_idx(sfd);
    if (idx < 0) return -(EBADF);
    if (g_sockets[idx].state == KSOCK_UNUSED) return -(EBADF);

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
        *addr_out = 0x0100007fU; /* 127.0.0.1 in network byte order */
        return 0;
    }

    /*
     * No DNS resolver yet. Return ENONET.
     * When lwIP + DNS is available, replace with dns_gethostbyname().
     */
    klog_info("[net] gethostbyname('%s') -> ENONET (no DNS)\n", name);
    *addr_out = 0;
    return -(ENONET);
}
