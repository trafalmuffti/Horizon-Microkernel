#pragma once

#include <stdint.h>

/*
 * NE2000 (DP8390) ISA network card driver for Horizon/i586.
 *
 * Default QEMU ISA NE2000 configuration:
 *   I/O base : 0x300
 *   IRQ      : 9
 *
 * Launch QEMU with: -netdev user,id=net0 -device ne2k_isa,netdev=net0
 */

/* ── Default hardware parameters ───────────────────────────────────────── */
#define NE2000_DEFAULT_BASE 0x300U
#define NE2000_IRQ          9

/* ── Page-0 register offsets (from I/O base) ───────────────────────────── */
#define NE_CR     0x00  /* Command Register                          */
#define NE_PSTART 0x01  /* Page Start  (W) / CLDA0 (R)              */
#define NE_PSTOP  0x02  /* Page Stop   (W) / CLDA1 (R)              */
#define NE_BNRY   0x03  /* Boundary Pointer                          */
#define NE_TPSR   0x04  /* TX Page Start (W) / TSR (R)              */
#define NE_TBCR0  0x05  /* TX Byte Count 0  (W)                     */
#define NE_TBCR1  0x06  /* TX Byte Count 1  (W)                     */
#define NE_ISR    0x07  /* Interrupt Status Register                 */
#define NE_RSAR0  0x08  /* Remote Start Address 0 (W) / CRDA0 (R)  */
#define NE_RSAR1  0x09  /* Remote Start Address 1 (W) / CRDA1 (R)  */
#define NE_RBCR0  0x0A  /* Remote Byte Count 0 (W)                  */
#define NE_RBCR1  0x0B  /* Remote Byte Count 1 (W)                  */
#define NE_RCR    0x0C  /* Receive Config Register (W) / RSR (R)    */
#define NE_TCR    0x0D  /* Transmit Config Register (W)             */
#define NE_DCR    0x0E  /* Data Config Register (W)                 */
#define NE_IMR    0x0F  /* Interrupt Mask Register (W)              */
#define NE_DATA   0x10  /* Data port (16-bit word access)           */
#define NE_RESET  0x1F  /* Reset port (read to reset, write to EOI) */

/* ── Page-1 register offsets ────────────────────────────────────────────── */
#define NE_PAR0   0x01  /* Physical Address bytes 0-5               */
#define NE_CURR   0x07  /* Current RX page pointer                  */
#define NE_MAR0   0x08  /* Multicast Address bytes 0-7              */

/* ── Command Register (CR) bits ─────────────────────────────────────────── */
#define CR_STP  0x01    /* Stop (must be set during init)           */
#define CR_STA  0x02    /* Start                                    */
#define CR_TXP  0x04    /* Transmit packet                          */
#define CR_RD0  0x08    /* Remote DMA bit 0                         */
#define CR_RD1  0x10    /* Remote DMA bit 1                         */
#define CR_RD2  0x20    /* Remote DMA bit 2                         */
#define CR_PS0  0x40    /* Page Select bit 0                        */
#define CR_PS1  0x80    /* Page Select bit 1                        */

/* Remote DMA command shortcuts */
#define CR_NODMA  (CR_RD2)               /* Abort / complete DMA   */
#define CR_RREAD  (CR_RD0)               /* Remote read            */
#define CR_RWRITE (CR_RD1)               /* Remote write           */

/* Page-select shortcuts */
#define CR_PAGE0  0x00
#define CR_PAGE1  CR_PS0
#define CR_PAGE2  CR_PS1

/* ── ISR bits ───────────────────────────────────────────────────────────── */
#define ISR_PRX 0x01    /* Packet received OK                       */
#define ISR_PTX 0x02    /* Packet transmitted OK                    */
#define ISR_RXE 0x04    /* Receive error                            */
#define ISR_TXE 0x08    /* Transmit error                           */
#define ISR_OVW 0x10    /* Receive buffer overflow                  */
#define ISR_CNT 0x20    /* Counter overflow                         */
#define ISR_RDC 0x40    /* Remote DMA complete                      */
#define ISR_RST 0x80    /* Reset status                             */

/* ── DCR bits ───────────────────────────────────────────────────────────── */
#define DCR_WTS 0x01    /* Word Transfer Select (1 = 16-bit)        */
#define DCR_BOS 0x02    /* Byte Order (0 = little-endian)           */
#define DCR_LAS 0x04    /* Long Address Select                      */
#define DCR_LS  0x08    /* Loopback Select                          */
#define DCR_ARM 0x10    /* Auto-init Remote DMA                     */
#define DCR_FT0 0x20    /* FIFO threshold bit 0                     */
#define DCR_FT1 0x40    /* FIFO threshold bit 1                     */

/* ── RCR bits ───────────────────────────────────────────────────────────── */
#define RCR_SEP 0x01    /* Save error packets                       */
#define RCR_AR  0x02    /* Accept runt packets                      */
#define RCR_AB  0x04    /* Accept broadcast                         */
#define RCR_AM  0x08    /* Accept multicast                         */
#define RCR_PRO 0x10    /* Promiscuous mode                         */
#define RCR_MON 0x20    /* Monitor mode (no packets buffered)       */

/* ── Internal memory layout ─────────────────────────────────────────────── */
/*
 * NE2000 (16-bit) has 16 KB of internal RAM.  Its byte address space
 * starts at 0x4000 inside the chip, which maps to page 0x40.
 *
 * We reserve six pages for TX and use the rest as the RX ring.
 */
#define NE_TX_PAGE   0x40   /* TX buffer start page                 */
#define NE_TX_PAGES  6      /* Number of pages reserved for TX      */
#define NE_RX_START  0x46   /* RX ring buffer start page            */
#define NE_RX_STOP   0x80   /* RX ring buffer stop page (exclusive) */

/* ── Packet receive header (prepended by the chip) ──────────────────────── */
typedef struct __attribute__((packed)) {
    uint8_t  status;      /* Receive status byte                    */
    uint8_t  next_page;   /* Page number of the next packet         */
    uint16_t length;      /* Total length including this header     */
} ne2000_rx_hdr_t;

/* ── Driver state ───────────────────────────────────────────────────────── */
typedef struct {
    uint16_t base;        /* I/O base address                       */
    uint8_t  mac[6];      /* MAC address read from on-card PROM     */
} ne2000_t;

/* ── Public API ─────────────────────────────────────────────────────────── */

/**
 * ne2000_init - Initialise the NE2000 card.
 * @dev : pointer to driver state; caller supplies base address.
 * Returns 0 on success, -1 if the card is not detected.
 */
int ne2000_init(ne2000_t *dev);

/**
 * ne2000_rx - Read one pending packet from the receive ring.
 * @dev : driver state.
 * @buf : caller-supplied buffer to store the Ethernet frame.
 * @maxlen : capacity of @buf in bytes.
 * Returns frame length on success, 0 if no packet is waiting, -1 on error.
 */
int ne2000_rx(ne2000_t *dev, void *buf, uint32_t maxlen);

/**
 * ne2000_tx - Transmit an Ethernet frame.
 * @dev : driver state.
 * @buf : frame data (including Ethernet header).
 * @len : frame length in bytes.
 * Returns @len on success, -1 on error.
 */
int ne2000_tx(ne2000_t *dev, const void *buf, uint16_t len);

/**
 * ne2000_irq_ack - Acknowledge a received hardware interrupt.
 * Call this from the IRQ handler after ne2000_rx() drains all packets.
 */
void ne2000_irq_ack(ne2000_t *dev);
