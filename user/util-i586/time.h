#pragma once

/*
 * Monotonic clock for Horizon userspace (i586), in microseconds.
 *
 * Resolution
 * ──────────
 * Round-trip times on a local link or an emulated NIC are routinely well
 * under a millisecond, so a millisecond-granular clock reports them as 0 or
 * 1 and conveys nothing. The counter below therefore accumulates in
 * microseconds, and clock_ms() is derived from it for callers that want
 * coarse time (wolfIP's poll loop only needs a forward-moving millisecond
 * value for its retransmit timers).
 *
 * Why not just divide the 64-bit TSC
 * ──────────────────────────────────
 * Userspace links with -nodefaultlibs, so libgcc is absent and any 64-bit
 * division emits an undefined reference to __udivdi3. The 32-bit `div`
 * instruction is generated inline, so every division here is 32-bit.
 *
 * We read the low 32 bits of the TSC and accumulate *deltas*. Unsigned
 * subtraction wraps correctly, so a delta stays accurate across a TSC
 * rollover as long as the clock is sampled more often than once per
 * rollover. Remainders are carried between calls, so no time is lost to
 * truncation.
 *
 * Where the rate comes from
 * ─────────────────────────
 * The TSC counts cycles, but its rate is not discoverable from userspace:
 * CPUID does not report it on this baseline, and the only fixed-rate
 * reference on the machine is the PIT, which the kernel owns for
 * scheduling. The kernel therefore measures the TSC against the PIT once at
 * boot and publishes the result, which tscfreq() reports in kHz.
 *
 * TSC_PER_US below is only the fallback used if that measurement is
 * unavailable. It is a guess, and a poor one on most hosts: a QEMU guest
 * generally sees the host's rate, measured at 2.1 GHz here rather than the
 * 100 MHz assumed. Timings taken on the fallback are sound in ordering and
 * proportion but not in absolute magnitude.
 */

#include <stdint.h>
#include <sys/svc.h>

#ifndef TSC_PER_US
#define TSC_PER_US 100U
#endif

static inline uint32_t tsc_read32(void)
{
    uint32_t lo, hi;
    __asm__ volatile ("rdtsc" : "=a"(lo), "=d"(hi));
    (void)hi;
    return lo;
}

/* Clock state. Header-only, so each program gets its own instance. */
static uint32_t _clk_per_us;     /* ticks per microsecond, from the kernel */
static uint32_t _clk_last_tsc;
static uint32_t _clk_tsc_carry;  /* TSC ticks not yet worth a microsecond  */
static uint32_t _clk_us_carry;   /* microseconds not yet worth a millisecond */
static uint32_t _clk_us;         /* microseconds since first call           */
static uint32_t _clk_ms;         /* milliseconds since first call           */
static int      _clk_started;

/*
 * _clk_update - fold elapsed TSC ticks into the microsecond and
 * millisecond counters. Must run at least once per TSC rollover
 * (~43 s at 100 MHz, ~1.4 s at 3 GHz) to stay accurate.
 */
static void _clk_update(void)
{
    uint32_t now = tsc_read32();
    uint32_t delta, us;

    if (!_clk_started) {
        /* Ask the kernel for the measured rate on first use. kHz/1000 is
         * ticks per microsecond; the truncated remainder is worth under
         * 0.001% and costs nothing to ignore. */
        uint32_t khz = tscfreq();

        _clk_per_us   = khz ? (khz / 1000U) : 0U;
        if (!_clk_per_us)
            _clk_per_us = TSC_PER_US;   /* uncalibrated fallback */

        _clk_started  = 1;
        _clk_last_tsc = now;
        return;
    }

    /* Unsigned subtraction yields the correct delta even across a wrap. */
    delta = now - _clk_last_tsc;
    _clk_last_tsc = now;

    _clk_tsc_carry += delta;
    us = _clk_tsc_carry / _clk_per_us;  /* 32-bit div: inlined by gcc */
    _clk_tsc_carry %= _clk_per_us;

    _clk_us += us;

    _clk_us_carry += us;
    _clk_ms += _clk_us_carry / 1000U;
    _clk_us_carry %= 1000U;
}

/* Microseconds since the first call. Wraps after ~71 minutes. */
static uint32_t clock_us(void)
{
    _clk_update();
    return _clk_us;
}

/* Milliseconds since the first call. */
static uint32_t clock_ms(void)
{
    _clk_update();
    return _clk_ms;
}
