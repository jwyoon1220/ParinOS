/*
 * src/net/net.h - ParinOS kernel socket layer
 *
 * Provides a BSD-socket-style interface between the syscall dispatcher
 * and the underlying network stack (lwIP or stub).
 *
 * Socket FDs live in [SOCK_FD_BASE, SOCK_FD_BASE+KSOCK_MAX), which is
 * disjoint from the VFS FD range [3, FD_TABLE_MAX).
 */

#ifndef PARINOS_NET_H
#define PARINOS_NET_H

#include <stdint.h>

/* ── Socket file descriptor base ─────────────────────────────────────── */
#define SOCK_FD_BASE  16   /* socket FDs: 16 .. 16+KSOCK_MAX-1 */
#define KSOCK_MAX      8   /* max concurrent sockets            */

/* ── Socket states ───────────────────────────────────────────────────── */
#define KSOCK_UNUSED     0
#define KSOCK_CREATED    1
#define KSOCK_BOUND      2
#define KSOCK_LISTENING  3
#define KSOCK_CONNECTED  4

/* ── Address/protocol families ───────────────────────────────────────── */
#define AF_INET    2

/* ── Socket types ────────────────────────────────────────────────────── */
#define SOCK_STREAM 1
#define SOCK_DGRAM  2

/* ── Kernel socket control block ─────────────────────────────────────── */
typedef struct {
    int      state;
    int      domain;        /* AF_INET */
    int      type;          /* SOCK_STREAM / SOCK_DGRAM */
    uint32_t local_addr;    /* IPv4, network byte order */
    uint16_t local_port;    /* host byte order          */
    uint32_t remote_addr;
    uint16_t remote_port;
} ksocket_t;

/* ── API ─────────────────────────────────────────────────────────────── */

/**
 * Create a new socket; returns a socket FD (>= SOCK_FD_BASE) or < 0 on error.
 */
int ksocket_create(int domain, int type);

/**
 * Bind a socket to a local address/port.
 * Returns 0 on success, negative errno on error.
 */
int ksocket_bind(int sfd, uint32_t addr, uint16_t port);

/**
 * Mark socket as passive (listening).
 */
int ksocket_listen(int sfd, int backlog);

/**
 * Accept an incoming connection.
 * Fills *client_addr and *client_port (may be NULL).
 * Returns a new socket FD or negative errno.
 */
int ksocket_accept(int sfd, uint32_t *client_addr, uint16_t *client_port);

/**
 * Connect to a remote address:port.
 * Returns 0 on success, negative errno on error.
 */
int ksocket_connect(int sfd, uint32_t addr, uint16_t port);

/**
 * Send data on a connected socket.
 * Returns bytes sent or negative errno.
 */
int ksocket_send(int sfd, const void *buf, uint32_t len);

/**
 * Receive data from a connected socket.
 * Returns bytes received, 0 for EOF, or negative errno.
 */
int ksocket_recv(int sfd, void *buf, uint32_t len);

/**
 * Close a socket and free its slot.
 */
int ksocket_close(int sfd);

/**
 * Resolve a hostname to an IPv4 address (network byte order).
 * Returns 0 on success, negative errno on error.
 */
int ksocket_gethostbyname(const char *name, uint32_t *addr_out);

/** Returns 1 if fd is a socket FD, 0 otherwise. */
static inline int is_socket_fd(int fd) {
    return fd >= SOCK_FD_BASE && fd < SOCK_FD_BASE + KSOCK_MAX;
}

#endif /* PARINOS_NET_H */
