/*
 * wolfip_port.c – Horizon platform hooks required by wolfIP.
 *
 * wolfip.h declares wolfIP_getrandom() under "External requirements": the
 * stack calls it but each port must supply it.  It seeds TCP initial
 * sequence numbers, ephemeral source ports and ICMP echo identifiers.
 */

#include <stdint.h>
#include "../util-i586/time.h"   /* tsc_read32() */

/*
 * xorshift32 PRNG, seeded and continuously re-stirred from the CPU
 * timestamp counter.
 *
 * Entropy caveat: Horizon exposes no hardware entropy source, so the TSC is
 * the only varying input available in userspace.  On an emulator its low
 * bits are only weakly unpredictable, which makes TCP initial sequence
 * numbers more guessable than on a general-purpose OS.  That is acceptable
 * for a hobby network stack on a trusted link, but this is deliberately not
 * a cryptographic RNG and must not be used to generate keys or nonces.
 */
static uint32_t rng_state;

uint32_t wolfIP_getrandom(void)
{
    uint32_t x;

    if (rng_state == 0) {
        /* Golden-ratio constant keeps the first state away from zero even
         * if the TSC reads back as 0 on an unusual platform. */
        rng_state = tsc_read32() ^ 0x9E3779B9U;
        if (rng_state == 0)
            rng_state = 0x9E3779B9U;
    }

    /* Mix in fresh timer jitter so repeated calls do not form a pure,
     * trivially replayable sequence from a single seed. */
    rng_state ^= tsc_read32();

    x = rng_state;
    x ^= x << 13;
    x ^= x >> 17;
    x ^= x << 5;
    rng_state = x;

    return x;
}
