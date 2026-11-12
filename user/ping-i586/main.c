/*
 * ping – ICMP echo utility for Horizon.
 *
 * Sends ICMP echo requests through the network daemon (netd, SVC_NET) and
 * reports replies with round-trip times.
 *
 * Usage (argv is not yet wired up by the Horizon loader, so the target is
 * taken from PING_TARGET below; pass a dotted-quad to skip DNS):
 *
 *     ping claude.ai
 *     ping 10.0.2.2
 *
 * Protocol notes
 * ──────────────
 * An ICMP socket in wolfIP carries the COMPLETE ICMP message rather than
 * just a payload.  We build:
 *
 *     type(1) code(1) checksum(2) id(2) seq(2) | payload
 *
 * netd forwards it verbatim; wolfIP overwrites the id with the socket's
 * own identifier and fills in the checksum before transmitting.  Replies
 * come back as the full ICMP message starting at the type byte.
 */

#include <sys/svc.h>
#include <sys/sched.h>
#include <sys/msg.h>
#include <horizon/ipc.h>
#include <stdint.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdio.h>
#include <string.h>
#include <malloc.h>

#include "../netd-i586/net_svc.h"
#include "../util-i586/time.h"

/* Host to ping. A dotted-quad is used directly; anything else is resolved. */
#ifndef PING_TARGET
#define PING_TARGET "claude.ai"
#endif

#define PING_COUNT      4       /* Number of echo requests to send  */
#define PING_PAYLOAD    32      /* Payload bytes after the header   */
#define PING_TIMEOUT_MS 4000    /* Per-request wait before giving up */

/*
 * TCP fallback probe.
 *
 * ICMP is filtered on many networks, so a total loss of echo replies does not
 * by itself mean the host is unreachable.  When every echo request goes
 * unanswered we fall back once to a TCP handshake against a port that is
 * commonly open.  A completed handshake proves the host is reachable and
 * routable even though ICMP is being dropped.
 */
#define PING_TCP_PORT   443     /* Probed once if no echo replies arrive */

/* ── Address helpers ────────────────────────────────────────────────────── */

static bool parse_ipv4(const char *s, uint32_t *out)
{
    uint32_t addr = 0;
    int      octet = 0, digits = 0, parts = 0;

    for (;; s++) {
        if (*s >= '0' && *s <= '9') {
            octet = octet * 10 + (*s - '0');
            if (octet > 255 || ++digits > 3)
                return false;
        } else if (*s == '.' || *s == '\0') {
            if (!digits)
                return false;
            addr = (addr << 8) | (uint32_t)octet;
            octet = 0;
            digits = 0;
            if (++parts > 4)
                return false;
            if (*s == '\0')
                break;
        } else {
            return false;
        }
    }
    if (parts != 4)
        return false;
    *out = addr;
    return true;
}

static void print_ipv4(uint32_t ip)
{
    printf("%u.%u.%u.%u",
           (unsigned)((ip >> 24) & 0xFF), (unsigned)((ip >> 16) & 0xFF),
           (unsigned)((ip >> 8)  & 0xFF), (unsigned)(ip & 0xFF));
}

/* ── Duration formatting ────────────────────────────────────────────────── */

/*
 * print_duration - render a microsecond count as milliseconds with three
 * decimal places, e.g. 437us -> "0.437 ms".
 *
 * Round-trips here are frequently sub-millisecond, so printing whole
 * milliseconds would collapse most results to "0 ms". libh's vprintf
 * implements no floating point, so the fractional part is composed from the
 * integer remainder and zero-padded by hand.
 */
static void print_duration(uint32_t usec)
{
    printf("%u.%u%u%u ms",
           (unsigned)(usec / 1000U),
           (unsigned)((usec / 100U) % 10U),
           (unsigned)((usec / 10U)  % 10U),
           (unsigned)(usec % 10U));
}

/* ── netd IPC helpers ───────────────────────────────────────────────────── */

static ipcport_t g_netd;

/*
 * net_call – issue a request to netd and wait for its reply.
 * Returns the reply code.  If @reply_buf is non-NULL, any payload the
 * daemon attaches is copied into it (up to @reply_cap bytes).
 */
static long net_call(msgdata_t code,
                     msgdata_t a0, msgdata_t a1, msgdata_t a2,
                     const void *payload, size_t payload_len,
                     void *reply_buf, size_t reply_cap)
{
    struct msg req, res;
    long       sz;

    memset(&req, 0, sizeof(req));
    req.to      = g_netd;
    req.code    = code;
    req.args[0] = a0;
    req.args[1] = a1;
    req.args[2] = a2;

    if (payload && payload_len) {
        req.payload.buf  = (void *)payload;
        req.payload.size = payload_len;
    }

    if (send(&req) < 0)
        return -1;

    /* Block until netd answers. Deferred replies (RECV/RESOLVE) land here
     * once the corresponding network event fires inside the daemon. */
    wait(g_netd);

    /* peek() reports failure as (size_t)-1, so read it through a signed
     * type before testing. */
    sz = (long)peek();
    memset(&res, 0, sizeof(res));

    if (sz > 0) {
        /* Always drain the payload so the message queue stays consistent,
         * even when the caller did not ask for the bytes. */
        size_t n = (size_t)sz;
        void *buf = malloc(n);
        if (!buf)
            return -1;
        res.payload.buf  = buf;
        res.payload.size = n;

        if (recv(&res) < 0) {
            free(buf);
            return -1;
        }
        if (reply_buf && reply_cap)
            memcpy(reply_buf, buf, n < reply_cap ? n : reply_cap);
        free(buf);
    } else if (recv(&res) < 0) {
        return -1;
    }

    return (long)(int32_t)res.code;
}

/* ── TCP fallback probe ─────────────────────────────────────────────────── */

/*
 * tcp_probe - complete a single TCP handshake against @ip:@port.
 *
 * Returns the handshake round-trip in microseconds, or -1 on failure.
 *
 * The connection is torn down immediately afterwards with one close().
 * Note that this sends a FIN rather than a reset: wolfIP's public API
 * exposes no abortive close, and struct wolfIP is opaque to us, so a
 * graceful shutdown is the only teardown available here.
 */
static long tcp_probe(uint32_t ip, uint16_t port)
{
    uint32_t t0, rtt_us;
    int      sock;

    sock = (int)net_call(NET_CMD_SOCKET, NET_SOCK_STREAM, 0, 0,
                         NULL, 0, NULL, 0);
    if (sock < 0)
        return -1;

    t0 = clock_us();

    /* NET_CMD_CONNECT only queues the SYN... */
    if (net_call(NET_CMD_CONNECT, (msgdata_t)sock, ip, port,
                 NULL, 0, NULL, 0) < 0) {
        net_call(NET_CMD_CLOSE, (msgdata_t)sock, 0, 0, NULL, 0, NULL, 0);
        return -1;
    }

    /* ...so wait for the handshake to actually complete. */
    if (net_call(NET_CMD_WAITCONN, (msgdata_t)sock, 0, 0,
                 NULL, 0, NULL, 0) < 0) {
        net_call(NET_CMD_CLOSE, (msgdata_t)sock, 0, 0, NULL, 0, NULL, 0);
        return -1;
    }

    rtt_us = clock_us() - t0;

    /* Tear the connection down once; we only needed the handshake. */
    net_call(NET_CMD_CLOSE, (msgdata_t)sock, 0, 0, NULL, 0, NULL, 0);

    return (long)rtt_us;
}

/* ── Entry point ────────────────────────────────────────────────────────── */

int main(void)
{
    const char *target = PING_TARGET;
    uint32_t    dest_ip = 0;
    int         sock;
    int         sent = 0, received = 0;
    uint32_t    rtt_total_us = 0, rtt_min_us = 0xFFFFFFFFU, rtt_max_us = 0;
    int         seq;

    /* Wait for the network daemon to come up. */
    while ((g_netd = svcid(SVC_NET)) == 0)
        yield();

    /* ── Resolve the target ── */
    if (!parse_ipv4(target, &dest_ip)) {
        long r = net_call(NET_CMD_RESOLVE, 0, 0, 0,
                          target, strlen(target) + 1, NULL, 0);
        if (r <= 0) {
            printf("ping: cannot resolve %s\n", target);
            return 1;
        }
        dest_ip = (uint32_t)r;
    }

    printf("PING %s (", target);
    print_ipv4(dest_ip);
    printf(") %u bytes of data.\n", (unsigned)PING_PAYLOAD);

    /* ── Open an ICMP socket and point it at the target ── */
    sock = (int)net_call(NET_CMD_SOCKET, NET_SOCK_ICMP, 0, 0, NULL, 0, NULL, 0);
    if (sock < 0) {
        printf("ping: cannot open ICMP socket\n");
        return 1;
    }

    /* connect() sets the remote address for subsequent sends. */
    if (net_call(NET_CMD_CONNECT, (msgdata_t)sock, dest_ip, 0,
                 NULL, 0, NULL, 0) < 0) {
        printf("ping: cannot set destination\n");
        net_call(NET_CMD_CLOSE, (msgdata_t)sock, 0, 0, NULL, 0, NULL, 0);
        return 1;
    }

    /* ── Echo request / reply loop ── */
    for (seq = 1; seq <= PING_COUNT; seq++) {
        uint8_t  packet[NET_ICMP_HDR_LEN + PING_PAYLOAD];
        uint8_t  reply[NET_ICMP_HDR_LEN + PING_PAYLOAD + 64];
        uint32_t t0, rtt_us;
        long     n;
        int      i;

        memset(packet, 0, sizeof(packet));
        packet[0] = NET_ICMP_ECHO_REQUEST; /* type */
        packet[1] = 0;                     /* code */
        packet[2] = 0;                     /* checksum - filled by wolfIP */
        packet[3] = 0;
        packet[4] = 0;                     /* id - overwritten by wolfIP */
        packet[5] = 0;
        packet[6] = (uint8_t)((seq >> 8) & 0xFF);
        packet[7] = (uint8_t)(seq & 0xFF);

        for (i = 0; i < PING_PAYLOAD; i++)
            packet[NET_ICMP_HDR_LEN + i] = (uint8_t)('a' + (i % 26));

        t0 = clock_us();

        if (net_call(NET_CMD_SEND, (msgdata_t)sock, 0, 0,
                     packet, sizeof(packet), NULL, 0) < 0) {
            printf("ping: send failed (seq=%d)\n", seq);
            continue;
        }
        sent++;

        memset(reply, 0, sizeof(reply));
        n = net_call(NET_CMD_RECV, (msgdata_t)sock, sizeof(reply), 0,
                     NULL, 0, reply, sizeof(reply));

        rtt_us = clock_us() - t0;

        if (n < (long)NET_ICMP_HDR_LEN) {
            printf("Request timeout for icmp_seq=%d\n", seq);
            continue;
        }
        if (reply[0] != NET_ICMP_ECHO_REPLY) {
            printf("Unexpected ICMP type %u for icmp_seq=%d\n",
                   (unsigned)reply[0], seq);
            continue;
        }

        received++;
        rtt_total_us += rtt_us;
        if (rtt_us < rtt_min_us) rtt_min_us = rtt_us;
        if (rtt_us > rtt_max_us) rtt_max_us = rtt_us;

        /* libh's vprintf implements no length modifiers, so %ld would
         * misparse; narrow to int explicitly. */
        printf("%d bytes from ", (int)n);
        print_ipv4(dest_ip);
        printf(": icmp_seq=%d time=", seq);
        print_duration(rtt_us);
        printf("\n");
    }

    net_call(NET_CMD_CLOSE, (msgdata_t)sock, 0, 0, NULL, 0, NULL, 0);

    /* ── Summary ── */
    printf("\n--- %s ping statistics ---\n", target);
    printf("%d packets transmitted, %d received, %d%% packet loss\n",
           sent, received,
           sent ? ((sent - received) * 100) / sent : 0);
    if (received) {
        printf("rtt min/avg/max = ");
        print_duration(rtt_min_us);
        printf(" / ");
        print_duration(rtt_total_us / (uint32_t)received);
        printf(" / ");
        print_duration(rtt_max_us);
        printf("\n");
        return 0;
    }

    /*
     * No echo replies. ICMP is frequently filtered, so before declaring the
     * host unreachable, probe once with TCP: a completed handshake shows the
     * host is reachable and that only ICMP is being dropped.
     */
    {
        long tcp_rtt_us;

        printf("\nNo ICMP replies; ICMP may be filtered.\n");
        printf("Probing ");
        print_ipv4(dest_ip);
        printf(":%u with TCP...\n", (unsigned)PING_TCP_PORT);

        tcp_rtt_us = tcp_probe(dest_ip, PING_TCP_PORT);

        if (tcp_rtt_us >= 0) {
            printf("tcp handshake to ");
            print_ipv4(dest_ip);
            printf(":%u succeeded in ", (unsigned)PING_TCP_PORT);
            print_duration((uint32_t)tcp_rtt_us);
            printf(" (connection closed)\n");
            printf("%s is reachable; ICMP echo is being dropped.\n", target);
            return 0;
        }

        printf("tcp handshake to ");
        print_ipv4(dest_ip);
        printf(":%u failed\n", (unsigned)PING_TCP_PORT);
        printf("%s is unreachable over both ICMP and TCP.\n", target);
    }

    return 1;
}
