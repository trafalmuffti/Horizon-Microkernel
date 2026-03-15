#ifndef WOLF_CONFIG_H
#define WOLF_CONFIG_H

/* wolfIP configuration for Horizon Microkernel (freestanding, no dynamic alloc) */

#define CONFIG_IPFILTER 0

#define ETHERNET
#define LINK_MTU 1500
#ifndef LINK_MTU_MIN
#define LINK_MTU_MIN 64U
#endif

/* Socket pool sizes - kept small for embedded use */
#define MAX_TCPSOCKETS  4
#define MAX_UDPSOCKETS  4
#define MAX_ICMPSOCKETS 2

/* Static ring buffer sizes */
#define RXBUF_SIZE (8 * 1024)
#define TXBUF_SIZE (8 * 1024)

#define MAX_NEIGHBORS 8

#define WOLFIP_MAX_INTERFACES    1
#define WOLFIP_ENABLE_FORWARDING 0
#define WOLFIP_ENABLE_LOOPBACK   0

/* Disable features that require POSIX or dynamic memory */
#undef WOLFIP_ENABLE_HTTP

/* Default static IP for QEMU user-mode networking */
#define WOLFIP_DEFAULT_IP   "10.0.2.15"
#define WOLFIP_DEFAULT_MASK "255.255.255.0"
#define WOLFIP_DEFAULT_GW   "10.0.2.2"

#endif /* WOLF_CONFIG_H */
