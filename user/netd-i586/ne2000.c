/*
 * ne2000.c - NE2000 (DP8390) ISA NIC driver for Horizon/i586.
 *
 * All hardware access goes through the Horizon sysio() system call, which
 * dispatches IO_INB / IO_INW / IO_OUTB / IO_OUTW to the privileged kernel
 * I/O helpers.  A driver process must hold PRIV_DRIVER privilege.
 */

#include "ne2000.h"
#include <sys/io.h>     /* sysio(), IO_INB, IO_INW, IO_OUTB, IO_OUTW */
#include <stddef.h>

/* ── Low-level port helpers ────────────────────────────────────────────── */

static inline uint8_t ne_inb(const ne2000_t *dev, uint8_t reg)
{
    return (uint8_t)sysio(IO_INB, (unsigned long)(dev->base + reg), NULL);
}

static inline uint16_t ne_inw(const ne2000_t *dev, uint8_t reg)
{
    return (uint16_t)sysio(IO_INW, (unsigned long)(dev->base + reg), NULL);
}

static inline void ne_outb(const ne2000_t *dev, uint8_t reg, uint8_t val)
{
    sysio(IO_OUTB, (unsigned long)(dev->base + reg), (void *)(unsigned long)val);
}

static inline void ne_outw(const ne2000_t *dev, uint8_t reg, uint16_t val)
{
    sysio(IO_OUTW, (unsigned long)(dev->base + reg), (void *)(unsigned long)val);
}

/* ── Remote DMA helpers ─────────────────────────────────────────────────── */

/*
 * ne2000_rdma_read - Copy @len bytes from NE2000 internal RAM at @src_addr
 * into the host buffer @dst.  @len is rounded up to the nearest even number
 * because the NE2000 performs 16-bit DMA.
 */
static void ne2000_rdma_read(const ne2000_t *dev,
                             uint16_t src_addr,
                             uint8_t  *dst,
                             uint16_t  len)
{
    uint16_t i;
    uint16_t rlen = (uint16_t)((len + 1U) & ~1U); /* round up to even */

    /* Program remote byte count and start address */
    ne_outb(dev, NE_RBCR0, (uint8_t)(rlen & 0xFFU));
    ne_outb(dev, NE_RBCR1, (uint8_t)(rlen >> 8));
    ne_outb(dev, NE_RSAR0, (uint8_t)(src_addr & 0xFFU));
    ne_outb(dev, NE_RSAR1, (uint8_t)(src_addr >> 8));

    /* Issue remote read DMA command */
    ne_outb(dev, NE_CR, CR_PAGE0 | CR_STA | CR_RREAD);

    /* Read 16-bit words from the data port */
    for (i = 0; i < rlen; i += 2) {
        uint16_t w = ne_inw(dev, NE_DATA);
        dst[i] = (uint8_t)(w & 0xFFU);
        if (i + 1U < len)
            dst[i + 1U] = (uint8_t)(w >> 8);
    }

    /* Wait for Remote DMA Complete */
    {
        int timeout = 20000;
        while (timeout-- > 0 && !(ne_inb(dev, NE_ISR) & ISR_RDC))
            ;
    }
    ne_outb(dev, NE_ISR, ISR_RDC);
}

/*
 * ne2000_rdma_write - Copy @len bytes from @src to NE2000 internal RAM at
 * @dst_addr.  @len is rounded up to even for 16-bit DMA.
 */
static void ne2000_rdma_write(const ne2000_t *dev,
                              uint16_t dst_addr,
                              const uint8_t *src,
                              uint16_t len)
{
    uint16_t i;
    uint16_t wlen = (uint16_t)((len + 1U) & ~1U);

    /* Clear RDC status bit before starting */
    ne_outb(dev, NE_ISR, ISR_RDC);

    ne_outb(dev, NE_RBCR0, (uint8_t)(wlen & 0xFFU));
    ne_outb(dev, NE_RBCR1, (uint8_t)(wlen >> 8));
    ne_outb(dev, NE_RSAR0, (uint8_t)(dst_addr & 0xFFU));
    ne_outb(dev, NE_RSAR1, (uint8_t)(dst_addr >> 8));

    ne_outb(dev, NE_CR, CR_PAGE0 | CR_STA | CR_RWRITE);

    for (i = 0; i < wlen; i += 2) {
        uint16_t w = (uint16_t)src[i];
        if (i + 1U < len)
            w |= (uint16_t)((uint16_t)src[i + 1U] << 8);
        ne_outw(dev, NE_DATA, w);
    }

    /* Wait for Remote DMA Complete */
    {
        int timeout = 20000;
        while (timeout-- > 0 && !(ne_inb(dev, NE_ISR) & ISR_RDC))
            ;
    }
    ne_outb(dev, NE_ISR, ISR_RDC);
}

/* ── Public API ─────────────────────────────────────────────────────────── */

int ne2000_init(ne2000_t *dev)
{
    uint8_t prom[16];
    int     i;

    /* --- Hardware reset -------------------------------------------------- */
    {
        uint8_t rst = ne_inb(dev, NE_RESET);
        ne_outb(dev, NE_RESET, rst);
        /* Brief delay */
        for (i = 0; i < 20000; i++)
            __asm__ volatile ("nop");
    }

    /* Wait until RST bit is set in ISR (card has completed reset) */
    {
        int timeout = 100000;
        while (timeout-- > 0 && !(ne_inb(dev, NE_ISR) & ISR_RST))
            ;
        if (!(ne_inb(dev, NE_ISR) & ISR_RST))
            return -1; /* Card not responding */
    }
    ne_outb(dev, NE_ISR, 0xFF); /* Clear all interrupt flags */

    /* --- Page 0 initialisation ------------------------------------------- */
    ne_outb(dev, NE_CR, CR_PAGE0 | CR_STP | CR_NODMA);

    /* 16-bit mode, FIFO threshold = 8 bytes (FT1) */
    ne_outb(dev, NE_DCR, DCR_WTS | DCR_FT1);

    /* Clear remote byte count */
    ne_outb(dev, NE_RBCR0, 0);
    ne_outb(dev, NE_RBCR1, 0);

    /* Monitor mode (no packet reception yet) */
    ne_outb(dev, NE_RCR, RCR_MON);

    /* Internal loopback during init */
    ne_outb(dev, NE_TCR, 0x02);

    /* TX page start */
    ne_outb(dev, NE_TPSR, NE_TX_PAGE);

    /* RX ring: PSTART / PSTOP / BNRY */
    ne_outb(dev, NE_PSTART, NE_RX_START);
    ne_outb(dev, NE_PSTOP,  NE_RX_STOP);
    ne_outb(dev, NE_BNRY,   NE_RX_START);

    /* Disable all interrupts for now */
    ne_outb(dev, NE_IMR, 0x00);

    /* --- Read MAC from on-card PROM (first 12 bytes at address 0x0000) ---- */
    /*
     * The NE2000 PROM is at the beginning of its address space (0x0000).
     * In 16-bit mode the MAC occupies the even bytes 0, 2, 4, 6, 8, 10.
     */
    ne2000_rdma_read(dev, 0x0000, prom, 12);
    for (i = 0; i < 6; i++)
        dev->mac[i] = prom[i * 2];

    /* --- Switch to page 1 to program the physical address register -------- */
    ne_outb(dev, NE_CR, CR_PAGE1 | CR_STP | CR_NODMA);

    for (i = 0; i < 6; i++)
        ne_outb(dev, (uint8_t)(NE_PAR0 + i), dev->mac[i]);

    /* Set current page to RX_START + 1 (leave first page for BNRY) */
    ne_outb(dev, NE_CURR, NE_RX_START + 1);

    /* Accept all multicast */
    for (i = 0; i < 8; i++)
        ne_outb(dev, (uint8_t)(NE_MAR0 + i), 0xFF);

    /* --- Back to page 0, normal operation --------------------------------- */
    ne_outb(dev, NE_CR, CR_PAGE0 | CR_STA | CR_NODMA);

    /* Disable loopback (normal external operation) */
    ne_outb(dev, NE_TCR, 0x00);

    /* Accept broadcast and unicast */
    ne_outb(dev, NE_RCR, RCR_AB);

    /* Enable RX/TX interrupts */
    ne_outb(dev, NE_IMR, ISR_PRX | ISR_PTX | ISR_RXE | ISR_TXE | ISR_OVW);

    return 0;
}

int ne2000_rx(ne2000_t *dev, void *buf, uint32_t maxlen)
{
    uint8_t         boundary, curr, next_page, new_bnry;
    ne2000_rx_hdr_t hdr;
    uint16_t        pkt_len;
    uint16_t        data_start;

    /* Read boundary (last page the driver read from) */
    boundary  = ne_inb(dev, NE_BNRY);
    next_page = (uint8_t)(boundary + 1U);
    if (next_page >= NE_RX_STOP)
        next_page = NE_RX_START;

    /* Read current page pointer from page 1 */
    ne_outb(dev, NE_CR, CR_PAGE1 | CR_STA | CR_NODMA);
    curr = ne_inb(dev, NE_CURR);
    ne_outb(dev, NE_CR, CR_PAGE0 | CR_STA | CR_NODMA);

    if (next_page == curr)
        return 0; /* No pending packet */

    /* Read the 4-byte receive status header */
    ne2000_rdma_read(dev,
                     (uint16_t)((uint16_t)next_page << 8),
                     (uint8_t *)&hdr,
                     sizeof(hdr));

    /* Sanity-check header */
    if (hdr.length < 4U || hdr.next_page > NE_RX_STOP ||
        hdr.next_page < NE_RX_START) {
        /* Corrupted header – reset the ring */
        ne_outb(dev, NE_CR,   CR_PAGE1 | CR_STA | CR_NODMA);
        curr = ne_inb(dev, NE_CURR);
        ne_outb(dev, NE_CR,   CR_PAGE0 | CR_STA | CR_NODMA);
        ne_outb(dev, NE_BNRY, (uint8_t)(curr - 1U >= NE_RX_START
                                        ? curr - 1U
                                        : NE_RX_STOP - 1U));
        return -1;
    }

    pkt_len = (uint16_t)(hdr.length - sizeof(hdr));
    data_start = (uint16_t)(((uint16_t)next_page << 8) + sizeof(hdr));

    if (pkt_len == 0U || pkt_len > (uint16_t)maxlen) {
        /* Skip oversized or empty packet */
        goto advance_bnry;
    }

    /*
     * The packet may wrap around the end of the RX ring.
     * Calculate how many bytes fit before the ring wraps.
     */
    {
        uint16_t end_of_ring  = (uint16_t)NE_RX_STOP << 8;
        uint16_t bytes_before_wrap;
        uint8_t  *dst = (uint8_t *)buf;

        if (data_start + pkt_len <= end_of_ring) {
            /* No wrap – single read */
            ne2000_rdma_read(dev, data_start, dst, pkt_len);
        } else {
            /* Packet wraps around ring end */
            bytes_before_wrap = (uint16_t)(end_of_ring - data_start);
            ne2000_rdma_read(dev, data_start, dst, bytes_before_wrap);
            ne2000_rdma_read(dev,
                             (uint16_t)NE_RX_START << 8,
                             dst + bytes_before_wrap,
                             (uint16_t)(pkt_len - bytes_before_wrap));
        }
    }

advance_bnry:
    /* Advance BNRY to the page before the next packet */
    new_bnry = (uint8_t)(hdr.next_page - 1U);
    if (new_bnry < NE_RX_START)
        new_bnry = (uint8_t)(NE_RX_STOP - 1U);
    ne_outb(dev, NE_BNRY, new_bnry);

    return (pkt_len == 0U || pkt_len > (uint16_t)maxlen) ? -1 : (int)pkt_len;
}

int ne2000_tx(ne2000_t *dev, const void *buf, uint16_t len)
{
    /* Write frame into TX buffer page in NE2000 internal RAM */
    ne2000_rdma_write(dev,
                      (uint16_t)NE_TX_PAGE << 8,
                      (const uint8_t *)buf,
                      len);

    /* Set TX parameters and trigger transmission */
    ne_outb(dev, NE_TPSR,  NE_TX_PAGE);
    ne_outb(dev, NE_TBCR0, (uint8_t)(len & 0xFFU));
    ne_outb(dev, NE_TBCR1, (uint8_t)(len >> 8));

    ne_outb(dev, NE_CR, CR_PAGE0 | CR_STA | CR_TXP);

    return (int)len;
}

void ne2000_irq_ack(ne2000_t *dev)
{
    uint8_t isr = ne_inb(dev, NE_ISR);
    ne_outb(dev, NE_ISR, isr); /* Write-back clears handled bits */

    /* Handle overflow: re-init the receive ring */
    if (isr & ISR_OVW) {
        uint8_t curr;

        ne_outb(dev, NE_CR, CR_PAGE0 | CR_STP | CR_NODMA);
        ne_outb(dev, NE_RBCR0, 0);
        ne_outb(dev, NE_RBCR1, 0);
        ne_outb(dev, NE_TCR, 0x02); /* loopback during reset */

        ne_outb(dev, NE_CR, CR_PAGE1 | CR_STA | CR_NODMA);
        curr = ne_inb(dev, NE_CURR);
        ne_outb(dev, NE_CR, CR_PAGE0 | CR_STA | CR_NODMA);

        ne_outb(dev, NE_BNRY, (uint8_t)(curr == NE_RX_START
                                        ? NE_RX_STOP - 1U
                                        : curr - 1U));
        ne_outb(dev, NE_ISR, ISR_OVW);
        ne_outb(dev, NE_TCR, 0x00); /* normal operation */
    }
}
