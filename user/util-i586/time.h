#pragma once

/*
 * Monotonic millisecond clock for Horizon userspace (i586).
 *
 * Why not just divide the 64-bit TSC
 * ─────────────────────────────────
 * Userspace programs are linked with -nodefaultlibs, so libgcc is absent and
 * any 64-bit division emits an undefined reference to __udivdi3.  The 32-bit
 * `div` instruction, by contrast, is generated inline by the compiler.
 *
 * So we read only the low 32 bits of the TSC and accumulate *deltas* into a
 * software counter.  Unsigned subtraction wraps correctly, so the delta stays
 * accurate across a TSC rollover (~43 s at 100 MHz) provided the clock is
 * sampled more frequently than that.  The remainder is carried between calls
 * so no time is lost to truncation.
 *
 * The resulting counter is monotonic, which is what wolfIP's poll loop needs;
 * absolute accuracy depends on TSC_PER_MS matching the real CPU frequency and
 * only affects the scale of retransmit timeouts and reported round-trip times.
 */

#include <stdint.h>

/*
 * TSC ticks per millisecond.  QEMU's emulated i586 runs at roughly 100 MHz;
 * override at build time with -DTSC_PER_MS=<n> to calibrate.
 */
#ifndef TSC_PER_MS
#define TSC_PER_MS 100000U
#endif

static inline uint32_t tsc_read32(void)
{
    uint32_t lo, hi;
    __asm__ volatile ("rdtsc" : "=a"(lo), "=d"(hi));
    (void)hi;
    return lo;
}

/* Clock state. Header-only, so each program gets its own instance. */
static uint32_t _ms_last_tsc;
static uint32_t _ms_carry;
static uint32_t _ms_value;
static int      _ms_started;

/*
 * clock_ms - milliseconds since the first call.
 * Must be called at least once per TSC rollover to stay accurate.
 */
static uint32_t clock_ms(void)
{
    uint32_t now = tsc_read32();
    uint32_t delta;

    if (!_ms_started) {
        _ms_started  = 1;
        _ms_last_tsc = now;
        return 0;
    }

    /* Unsigned subtraction yields the correct delta even across a wrap. */
    delta = now - _ms_last_tsc;
    _ms_last_tsc = now;

    _ms_carry += delta;
    _ms_value += _ms_carry / TSC_PER_MS;   /* 32-bit div: inlined by gcc */
    _ms_carry %= TSC_PER_MS;

    return _ms_value;
}
