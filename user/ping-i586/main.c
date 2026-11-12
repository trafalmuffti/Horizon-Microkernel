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

/* ── Entry point ────────────────────────────────────────────────────────── */

int main(void)
{
    const char *target = PING_TARGET;
    uint32_t    dest_ip = 0;
    int         sock;
    int         sent = 0, received = 0;
    uint32_t    rtt_total = 0, rtt_min = 0xFFFFFFFFU, rtt_max = 0;
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
        uint32_t t0, rtt;
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

        t0 = clock_ms();

        if (net_call(NET_CMD_SEND, (msgdata_t)sock, 0, 0,
                     packet, sizeof(packet), NULL, 0) < 0) {
            printf("ping: send failed (seq=%d)\n", seq);
            continue;
        }
        sent++;

        memset(reply, 0, sizeof(reply));
        n = net_call(NET_CMD_RECV, (msgdata_t)sock, sizeof(reply), 0,
                     NULL, 0, reply, sizeof(reply));

        rtt = clock_ms() - t0;

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
        rtt_total += rtt;
        if (rtt < rtt_min) rtt_min = rtt;
        if (rtt > rtt_max) rtt_max = rtt;

        printf("%ld bytes from ", n);
        print_ipv4(dest_ip);
        printf(": icmp_seq=%d time=%u ms\n", seq, (unsigned)rtt);
    }

    net_call(NET_CMD_CLOSE, (msgdata_t)sock, 0, 0, NULL, 0, NULL, 0);

    /* ── Summary ── */
    printf("\n--- %s ping statistics ---\n", target);
    printf("%d packets transmitted, %d received, %d%% packet loss\n",
           sent, received,
           sent ? ((sent - received) * 100) / sent : 0);
    if (received)
        printf("rtt min/avg/max = %u/%u/%u ms\n",
               (unsigned)rtt_min,
               (unsigned)(rtt_total / (uint32_t)received),
               (unsigned)rtt_max);

    return received ? 0 : 1;
}
