#pragma once

/*
 * net_svc.h – Network service IPC protocol for Horizon.
 *
 * The network daemon (netd) registers as SVC_NET and accepts the message
 * codes defined below.  All IPC is synchronous: the client sends a request
 * and blocks until netd replies.
 *
 * For operations that may block (NET_CMD_ACCEPT, NET_CMD_RECV), netd defers
 * the reply until data is available via the wolfIP event callback, so the
 * calling thread stays blocked in wait() until the network event fires.
 *
 * Encoding conventions
 * --------------------
 *  - IP addresses are in host byte-order (uint32_t, MSB = first octet).
 *  - Ports are in host byte-order (uint16_t).
 *  - Socket file-descriptors are the raw values returned by wolfIP; they
 *    encode the socket type in the upper bits (see wolfip.h MARK_*_SOCKET).
 */

/* ── Socket types (args[0] for NET_CMD_SOCKET) ─────────────────────────── */
#define NET_SOCK_STREAM 1   /* TCP  */
#define NET_SOCK_DGRAM  2   /* UDP  */
#define NET_SOCK_ICMP   3   /* ICMP (used by ping) */

/*
 * ICMP message layout, for clients building echo requests.
 *
 * An ICMP socket carries the COMPLETE ICMP message, not just a payload:
 * the client supplies type/code/checksum/id/seq followed by its payload.
 * netd forwards it verbatim to wolfIP, which overwrites the id field with
 * the socket's identifier and computes the checksum before transmission.
 * NET_CMD_RECV returns the full ICMP message of the reply, starting at the
 * type byte.
 */
#define NET_ICMP_HDR_LEN       8
#define NET_ICMP_ECHO_REQUEST  8
#define NET_ICMP_ECHO_REPLY    0

/* ── Request codes (msg.code) ───────────────────────────────────────────── */

/*
 * NET_CMD_IPCONFIG – Set the static IP configuration.
 *   args[0] = IPv4 address (host byte order)
 *   args[1] = subnet mask  (host byte order)
 *   args[2] = default gateway (host byte order)
 *   Returns: 0 on success.
 */
#define NET_CMD_IPCONFIG 0

/*
 * NET_CMD_SOCKET – Allocate a new socket.
 *   args[0] = socket type (NET_SOCK_STREAM or NET_SOCK_DGRAM)
 *   Returns: socket fd (>= 0) or negative errno on failure.
 */
#define NET_CMD_SOCKET   1

/*
 * NET_CMD_BIND – Bind a socket to a local port.
 *   args[0] = socket fd
 *   args[1] = local IPv4 address (0 = INADDR_ANY)
 *   args[2] = local port (host byte order)
 *   Returns: 0 on success, negative errno on failure.
 */
#define NET_CMD_BIND     2

/*
 * NET_CMD_LISTEN – Mark a TCP socket as passive.
 *   args[0] = socket fd
 *   Returns: 0 on success, negative errno on failure.
 */
#define NET_CMD_LISTEN   3

/*
 * NET_CMD_ACCEPT – Accept an incoming TCP connection.
 *   args[0] = listening socket fd
 *   Blocks until a connection arrives (deferred reply).
 *   Returns: new connected socket fd, or negative errno.
 */
#define NET_CMD_ACCEPT   4

/*
 * NET_CMD_CONNECT – Initiate a TCP connection (non-blocking start).
 *   args[0] = socket fd
 *   args[1] = remote IPv4 address (host byte order)
 *   args[2] = remote port (host byte order)
 *   Returns: 0 if the SYN was queued, negative errno on error.
 *   Note: the connection completes asynchronously; use NET_CMD_SEND to
 *         detect completion (wolfIP will return -EAGAIN until connected).
 */
#define NET_CMD_CONNECT  5

/*
 * NET_CMD_SEND – Send data on a socket.
 *   args[0] = socket fd
 *   payload = data to send
 *   Returns: bytes sent (>= 0) or negative errno.
 */
#define NET_CMD_SEND     6

/*
 * NET_CMD_RECV – Receive data from a socket.
 *   args[0] = socket fd
 *   args[1] = maximum number of bytes to receive (capped at 2048)
 *   Blocks until data is available (deferred reply).
 *   Returns: bytes received (payload contains data), or negative errno.
 */
#define NET_CMD_RECV     7

/*
 * NET_CMD_CLOSE – Close a socket.
 *   args[0] = socket fd
 *   Returns: 0 on success.
 */
#define NET_CMD_CLOSE    8

/*
 * NET_CMD_RESOLVE – Resolve a hostname to an IPv4 address via DNS.
 *   payload = NUL-terminated hostname (e.g. "claude.ai")
 *   Blocks until the DNS reply arrives (deferred reply).
 *   Returns: IPv4 address in host byte order, or 0 on failure.
 *
 *   Only one DNS query may be in flight at a time; a second concurrent
 *   request is rejected with 0 while the first is pending.
 */
#define NET_CMD_RESOLVE  9

/*
 * NET_CMD_WAITCONN – Wait for a TCP connection to finish its handshake.
 *   args[0] = socket fd previously passed to NET_CMD_CONNECT
 *   Blocks until the socket becomes writable (deferred reply), which wolfIP
 *   signals via CB_EVENT_WRITABLE once the handshake completes.
 *   Returns: 0 once connected, negative if the peer closed or reset first.
 *
 *   NET_CMD_CONNECT only queues the SYN, so this is how a client learns the
 *   connection actually came up. Used by ping's TCP fallback probe.
 */
#define NET_CMD_WAITCONN 10
