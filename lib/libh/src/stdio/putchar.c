#include <stdio.h>
#include <sys/io.h>

/*
 * Serial console output.
 *
 * Horizon has no general stdout yet: the intended path is the vgatty driver
 * reached through the VFS, but that is not wired up, so this previously
 * returned 0 and every printf() in userspace was silently discarded.
 *
 * Writing to COM1 gives userspace a working console with no dependency on
 * another server being up, which makes it usable from boot modules and from
 * early startup before any driver has registered.
 *
 * Two constraints are worth knowing:
 *
 *  - sysio() is privileged, so this works for processes with PRIV_DRIVER.
 *    Boot modules get PRIV_DRIVER, so they can print; an unprivileged child
 *    gets -EPRIV back from the kernel and the character is dropped rather
 *    than faulting.
 *
 *  - The UART itself is configured by the kernel's own serial debug init,
 *    so no line/baud setup is repeated here.
 */

#define COM1_BASE 0x3F8
#define COM1_LSR  (COM1_BASE + 5)   /* Line Status Register        */
#define LSR_THRE  0x20              /* Transmit holding reg. empty */

__hdecl int putchar(char c)
{
	/*
	 * Wait for the transmit register to drain, bounded so a wedged or
	 * absent UART cannot hang the caller forever.
	 */
	for (int spin = 0; spin < 100000; ++spin)
	{
		long status = sysio(IO_INB, COM1_LSR, NULL);
		if (status < 0)
			return 0;        /* Not permitted; drop the character. */
		if (status & LSR_THRE)
			break;
	}

	if (sysio(IO_OUTB, COM1_BASE, (void*)(unsigned long)(unsigned char)c) < 0)
		return 0;

	/* Terminals expect CRLF; translate so output is not stair-stepped. */
	if (c == '\n')
		putchar('\r');

	return c;
}
