/*
 * main.c – Horizon network daemon (netd).
 *
 * netd drives a NE2000 ISA NIC and exposes a BSD-like socket interface to
 * other Horizon processes via the SVC_NET IPC service.
 *
 * Architecture
 * ────────────
 *  1. Initialise the NE2000 driver (reads MAC from on-card PROM).
 *  2. Initialise wolfIP (static allocation, no heap required).
 *  3. Wire the NE2000 poll/send callbacks into the wolfIP link-layer device.
 *  4. Register as SVC_NET so other processes can find us via svcid(SVC_NET).
 *  5. Claim IRQ 9 (NE2000 default) so the kernel delivers hardware interrupts
 *     as messages from IPORT_KERNEL.
 *  6. Event loop: block on wait(IPORT_ANY); on wakeup either
 *       • process a hardware interrupt (drain RX ring, tick wolfIP), or
 *       • dispatch a socket IPC request from a client process.
 *
 * Deferred replies
 * ────────────────
 * wolfIP is non-blocking.  If NET_CMD_ACCEPT or NET_CMD_RECV would block
 * (wolfIP returns -EAGAIN), netd saves the caller's IPC port in a per-socket
 * waiter table and delays the reply until wolfIP fires a CB_EVENT_READABLE
 * callback for that socket.
 *
 * Timer source
 * ────────────
 * wolfIP_poll() requires a monotonically increasing millisecond counter.  We
 * derive it from the x86 TSC (RDTSC), assuming the QEMU-emulated i586 runs
 * at ~100 MHz.  The constant is only used for retransmit timeouts; slightly
 * wrong values just mean slightly different backoff intervals.
 */

#include <sys/svc.h>
#include <sys/sched.h>
#include <sys/msg.h>
#include <sys/io.h>
#include <horizon/ipc.h>
#include <stdbool.h>
#include <stddef.h>
#include <string.h>
#include <malloc.h>

#include "ne2000.h"
#include "net_svc.h"
#include "wolfip.h"  /* Horizon wrapper – silences LOG, sets freestanding config */

/* ── Configuration ──────────────────────────────────────────────────────── */

#define NE2000_BASE  NE2000_DEFAULT_BASE  /* 0x300 */
#define NE2000_IRQ_N NE2000_IRQ           /* 9     */

/* Maximum payload bytes for a deferred recv reply */
#define RECV_BUF_SIZE 2048U

/* Maximum concurrent open sockets (index is SOCKET_UNMARK(fd)) */
#define MAX_OPEN_SOCKETS 256

/* ── Timing (RDTSC-based ms counter) ────────────────────────────────────── */

static inline uint64_t rdtsc64(void)
{
    uint32_t lo, hi;
    __asm__ volatile ("rdtsc" : "=a"(lo), "=d"(hi));
    return ((uint64_t)hi << 32) | (uint64_t)lo;
}

/*
 * get_ms: approximate milliseconds since boot.
 * Assumes ~100 MHz TSC (QEMU i586 default).  Accuracy is not critical;
 * wolfIP only needs a forward-moving counter for retransmit timers.
 */
static uint32_t get_ms(void)
{
    return (uint32_t)(rdtsc64() / 100000ULL);
}

/* ── Global state ───────────────────────────────────────────────────────── */

static ne2000_t    g_ne;         /* NE2000 driver state                      */
static struct wolfIP *g_ip;     /* wolfIP stack instance (static allocation) */

/*
 * Per-socket waiter table.
 * Indexed by the raw socket index (SOCKET_UNMARK(fd)).
 * A non-zero waiter means a client is blocked on this socket.
 */
typedef struct {
    ipcport_t waiter;      /* IPC port of the blocked client (0 = none)   */
    int       wait_cmd;    /* Which command is pending                     */
    uint32_t  wait_maxlen; /* For NET_CMD_RECV: requested byte count       */
} sock_waiter_t;

static sock_waiter_t g_waiters[MAX_OPEN_SOCKETS];

/* ── wolfIP link-layer callbacks ────────────────────────────────────────── */

static int ll_poll(struct wolfIP_ll_dev *ll, void *buf, uint32_t len)
{
    (void)ll;
    return ne2000_rx(&g_ne, buf, len);
}

static int ll_send(struct wolfIP_ll_dev *ll, void *buf, uint32_t len)
{
    (void)ll;
    return ne2000_tx(&g_ne, buf, (uint16_t)(len > 0xFFFFU ? 0xFFFFU : len));
}

/* ── wolfIP socket event callback ───────────────────────────────────────── */

static void socket_event_cb(int fd, uint16_t events, void *arg)
{
    int           idx = SOCKET_UNMARK(fd);
    sock_waiter_t *w;
    struct msg    resp;

    (void)arg;

    if (idx < 0 || idx >= MAX_OPEN_SOCKETS)
        return;

    w = &g_waiters[idx];
    if (!w->waiter)
        return;

    /* ── Deferred accept ── */
    if ((events & CB_EVENT_READABLE) && w->wait_cmd == NET_CMD_ACCEPT) {
        struct wolfIP_sockaddr_in addr;
        socklen_t addrlen = sizeof(addr);
        int new_fd = wolfIP_sock_accept(g_ip, fd,
                                        (struct wolfIP_sockaddr *)&addr,
                                        &addrlen);
        if (new_fd >= 0) {
            wolfIP_register_callback(g_ip, new_fd, socket_event_cb, NULL);
            memset(&resp, 0, sizeof(resp));
            resp.to   = w->waiter;
            resp.code = (msgdata_t)new_fd;
            send(&resp);
            w->waiter = 0;
        }
        return;
    }

    /* ── Deferred recv ── */
    if ((events & CB_EVENT_READABLE) && w->wait_cmd == NET_CMD_RECV) {
        static uint8_t recv_buf[RECV_BUF_SIZE];
        uint32_t want = w->wait_maxlen;
        if (want > RECV_BUF_SIZE)
            want = RECV_BUF_SIZE;

        int ret = wolfIP_sock_recv(g_ip, fd, recv_buf, want, 0);
        if (ret > 0) {
            memset(&resp, 0, sizeof(resp));
            resp.to            = w->waiter;
            resp.code          = (msgdata_t)ret;
            resp.payload.buf   = recv_buf;
            resp.payload.size  = (size_t)ret;
            send(&resp);
            w->waiter = 0;
        }
        return;
    }

    /* ── Connection closed while client was waiting ── */
    if (events & CB_EVENT_CLOSED) {
        memset(&resp, 0, sizeof(resp));
        resp.to   = w->waiter;
        resp.code = (msgdata_t)(uint32_t)-1; /* error sentinel */
        send(&resp);
        w->waiter = 0;
    }
}

/* ── IPC request dispatcher ─────────────────────────────────────────────── */

static void handle_request(const struct msg *req)
{
    struct msg resp;
    memset(&resp, 0, sizeof(resp));
    resp.to   = req->from;
    resp.code = (msgdata_t)(uint32_t)-1; /* default: error */

    switch (req->code) {

    /* ── NET_CMD_IPCONFIG ── */
    case NET_CMD_IPCONFIG: {
        ip4 ip   = (ip4)req->args[0];
        ip4 mask = (ip4)req->args[1];
        ip4 gw   = (ip4)req->args[2];
        wolfIP_ipconfig_set(g_ip, ip, mask, gw);
        resp.code = 0;
        send(&resp);
        break;
    }

    /* ── NET_CMD_SOCKET ── */
    case NET_CMD_SOCKET: {
        int wtype = ((int)req->args[0] == NET_SOCK_STREAM)
                    ? IPSTACK_SOCK_STREAM
                    : IPSTACK_SOCK_DGRAM;
        int fd = wolfIP_sock_socket(g_ip, AF_INET, wtype, 0);
        if (fd >= 0) {
            int idx = SOCKET_UNMARK(fd);
            if (idx < MAX_OPEN_SOCKETS) {
                g_waiters[idx].waiter = 0;
                wolfIP_register_callback(g_ip, fd, socket_event_cb, NULL);
            }
        }
        resp.code = (msgdata_t)fd;
        send(&resp);
        break;
    }

    /* ── NET_CMD_BIND ── */
    case NET_CMD_BIND: {
        int       fd   = (int)req->args[0];
        uint32_t  ip   = req->args[1];
        uint16_t  port = (uint16_t)req->args[2];
        struct wolfIP_sockaddr_in addr;
        memset(&addr, 0, sizeof(addr));
        addr.sin_family       = AF_INET;
        addr.sin_port         = ee16(port);
        addr.sin_addr.s_addr  = ee32(ip);
        resp.code = (msgdata_t)wolfIP_sock_bind(g_ip, fd,
                                                (struct wolfIP_sockaddr *)&addr,
                                                sizeof(addr));
        send(&resp);
        break;
    }

    /* ── NET_CMD_LISTEN ── */
    case NET_CMD_LISTEN: {
        int fd = (int)req->args[0];
        resp.code = (msgdata_t)wolfIP_sock_listen(g_ip, fd, 4);
        send(&resp);
        break;
    }

    /* ── NET_CMD_ACCEPT (may defer) ── */
    case NET_CMD_ACCEPT: {
        int fd = (int)req->args[0];
        struct wolfIP_sockaddr_in addr;
        socklen_t addrlen = sizeof(addr);
        int new_fd = wolfIP_sock_accept(g_ip, fd,
                                        (struct wolfIP_sockaddr *)&addr,
                                        &addrlen);
        if (new_fd >= 0) {
            int idx = SOCKET_UNMARK(new_fd);
            if (idx < MAX_OPEN_SOCKETS) {
                g_waiters[idx].waiter = 0;
                wolfIP_register_callback(g_ip, new_fd, socket_event_cb, NULL);
            }
            resp.code = (msgdata_t)new_fd;
            send(&resp);
        } else {
            /* Defer: save waiter and do not send a reply yet */
            int idx = SOCKET_UNMARK(fd);
            if (idx >= 0 && idx < MAX_OPEN_SOCKETS) {
                g_waiters[idx].waiter   = req->from;
                g_waiters[idx].wait_cmd = NET_CMD_ACCEPT;
            } else {
                send(&resp); /* Can't defer, send error */
            }
        }
        break;
    }

    /* ── NET_CMD_CONNECT ── */
    case NET_CMD_CONNECT: {
        int      fd   = (int)req->args[0];
        uint32_t ip   = req->args[1];
        uint16_t port = (uint16_t)req->args[2];
        struct wolfIP_sockaddr_in addr;
        memset(&addr, 0, sizeof(addr));
        addr.sin_family       = AF_INET;
        addr.sin_port         = ee16(port);
        addr.sin_addr.s_addr  = ee32(ip);
        resp.code = (msgdata_t)wolfIP_sock_connect(g_ip, fd,
                                                   (struct wolfIP_sockaddr *)&addr,
                                                   sizeof(addr));
        send(&resp);
        break;
    }

    /* ── NET_CMD_SEND ── */
    case NET_CMD_SEND: {
        int fd = (int)req->args[0];
        if (!req->payload.buf || req->payload.size == 0U) {
            resp.code = 0;
            send(&resp);
            break;
        }
        resp.code = (msgdata_t)wolfIP_sock_send(g_ip, fd,
                                                req->payload.buf,
                                                req->payload.size,
                                                0);
        send(&resp);
        break;
    }

    /* ── NET_CMD_RECV (may defer) ── */
    case NET_CMD_RECV: {
        static uint8_t recv_buf[RECV_BUF_SIZE];
        int      fd     = (int)req->args[0];
        uint32_t maxlen = req->args[1];
        int      ret;

        if (maxlen == 0U || maxlen > RECV_BUF_SIZE)
            maxlen = RECV_BUF_SIZE;

        ret = wolfIP_sock_recv(g_ip, fd, recv_buf, maxlen, 0);
        if (ret > 0) {
            resp.code          = (msgdata_t)ret;
            resp.payload.buf   = recv_buf;
            resp.payload.size  = (size_t)ret;
            send(&resp);
        } else if (ret == -(WOLFIP_EAGAIN)) {
            /* Defer */
            int idx = SOCKET_UNMARK(fd);
            if (idx >= 0 && idx < MAX_OPEN_SOCKETS) {
                g_waiters[idx].waiter      = req->from;
                g_waiters[idx].wait_cmd    = NET_CMD_RECV;
                g_waiters[idx].wait_maxlen = maxlen;
            } else {
                send(&resp);
            }
        } else {
            resp.code = (msgdata_t)ret;
            send(&resp);
        }
        break;
    }

    /* ── NET_CMD_CLOSE ── */
    case NET_CMD_CLOSE: {
        int fd  = (int)req->args[0];
        int idx = SOCKET_UNMARK(fd);
        if (idx >= 0 && idx < MAX_OPEN_SOCKETS)
            g_waiters[idx].waiter = 0;
        resp.code = (msgdata_t)wolfIP_sock_close(g_ip, fd);
        send(&resp);
        break;
    }

    default:
        send(&resp);
        break;
    }
}

/* ── Entry point ────────────────────────────────────────────────────────── */

int main(void)
{
    int i;

    /* ── 1. Initialise NE2000 ── */
    memset(&g_ne, 0, sizeof(g_ne));
    g_ne.base = NE2000_BASE;
    if (ne2000_init(&g_ne) < 0)
        return 1; /* Card not found */

    /* ── 2. Initialise wolfIP (static allocation, no malloc) ── */
    wolfIP_init_static(&g_ip);

    /* ── 3. Wire NE2000 into wolfIP link-layer device ── */
    {
        struct wolfIP_ll_dev *ll = wolfIP_getdev(g_ip);
        for (i = 0; i < 6; i++)
            ll->mac[i] = g_ne.mac[i];
        memcpy(ll->ifname, "ne0", 4);
        ll->mtu          = 1500;
        ll->non_ethernet = 0;
        ll->poll         = ll_poll;
        ll->send         = ll_send;
    }

    /* ── 4. Static IP configuration (QEMU user-mode defaults) ── */
    wolfIP_ipconfig_set(g_ip,
                        atoip4(WOLFIP_DEFAULT_IP),
                        atoip4(WOLFIP_DEFAULT_MASK),
                        atoip4(WOLFIP_DEFAULT_GW));

    /* ── 5. Clear waiter table ── */
    memset(g_waiters, 0, sizeof(g_waiters));

    /* ── 6. Register as SVC_NET ── */
    if (svcown(SVC_NET) < 0)
        return 1;

    /* ── 7. Claim NE2000 IRQ so the kernel delivers packets as messages ── */
    if (svcown(SVC_IRQ(NE2000_IRQ_N)) < 0)
        return 1;

    /* ── 8. Main event loop ── */
    while (true) {
        struct msg req;
        size_t     payload_sz;

        /* Block until an IRQ fires or an IPC message arrives */
        wait(IPORT_ANY);

        payload_sz = peek();
        memset(&req, 0, sizeof(req));

        if (payload_sz > 0) {
            req.payload.buf  = malloc(payload_sz);
            req.payload.size = payload_sz;
        }

        if (recv(&req) < 0) {
            if (req.payload.buf)
                free(req.payload.buf);
            continue;
        }

        if (req.from == IPORT_KERNEL) {
            /*
             * Hardware interrupt from NE2000 (IRQ 9).
             * Drain all pending RX packets, tick the stack, then ACK.
             */
            static uint8_t pkt_buf[1536];
            int pkt_len;
            uint32_t now = get_ms();

            while ((pkt_len = ne2000_rx(&g_ne, pkt_buf, sizeof(pkt_buf))) > 0)
                wolfIP_recv(g_ip, pkt_buf, (uint32_t)pkt_len);

            wolfIP_poll(g_ip, now);
            ne2000_irq_ack(&g_ne);
        } else {
            /* IPC socket request from a user process */
            wolfIP_poll(g_ip, get_ms()); /* tick before handling */
            handle_request(&req);
            wolfIP_poll(g_ip, get_ms()); /* tick after handling */
        }

        if (req.payload.buf)
            free(req.payload.buf);
    }

    return 0;
}
