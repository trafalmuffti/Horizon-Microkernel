/* Horizon Microkernel - Hobby Operating System
 * Copyright (C) 2014 Wyatt Lindquist
 * 
 * This program is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
 * (at your option) any later version.
 * 
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 * 
 * You should have received a copy of the GNU General Public License
 * along with this program.  If not, see <http://www.gnu.org/licenses/>.
 */

#include "pit.h"
#include <sw/instr/misc.h>
#include <debug/error.h>

//! Setup a PIT timer with a given frequency and output mode.
void pit_timer_set(PIT_TIMER timer, size_t freq, PIT_OUTMODE mode)
{
	dassert(freq);

	// Set up the command with some defaults + the params.
	pit_command_t cmd = {0};
	cmd.count_mode  = PIT_CNTMODE_BIN;
	cmd.output_mode = mode;
	cmd.access_mode = PIT_ACCESS_RW_BOTH;
	cmd.channel     = timer;

	pit_write(cmd, freq);
}

//! Measure the CPU timestamp counter against the PIT.
/*! The TSC counts core cycles but its rate is not discoverable on this
 *  baseline: CPUID leaves 0x15/0x16 postdate the i586, and under emulation
 *  the counter generally advances at the host's rate rather than any
 *  nominal figure. The PIT, by contrast, is driven by a fixed 1.193182 MHz
 *  crystal, so counting cycles across a known PIT interval yields the rate.
 *
 *  Channel 2 is used because it is gateable and its output is readable
 *  without an interrupt, the same mechanism pit_sleep() relies on.
 *
 *  Returns kHz, or 0 if the PIT never signalled. */
uint32_t pit_calibrate_tsc()
{
	// ~50ms per round: long enough to swamp sampling overhead, short
	// enough that the 16-bit divider does not overflow (max ~54.9ms).
	const size_t   freq    = 20;
	const uint32_t divider = PIT_BASE_FREQ / freq;

	uint64_t best = 0;

	for (int round = 0; round != 3; ++round)
	{
		pit_timer_set(PIT_TIMER2, freq, PIT_OUTMODE_ONESHOT);

		// Toggle the CH2 gate to start the one-shot.
		pit_control_t ctl = pit_control_read();
		ctl.gate_ch2 = 0;
		pit_control_write(ctl);
		ctl.gate_ch2 = 1;
		pit_control_write(ctl);

		uint64_t start = rdtsc64();

		// Guard the wait: a PIT that never raises its output would
		// otherwise hang the boot outright.
		size_t guard = 0;
		while (pit_control_read().out_ch2 == 0)
		{
			if (++guard > 100000000u)
				return 0;
			pause();
		}

		uint64_t elapsed = rdtsc64() - start;

		// Anything that perturbs a round can only lengthen it, so the
		// shortest round is the least contaminated estimate.
		if (!best || elapsed < best)
			best = elapsed;
	}

	/* Cycles per millisecond is already kHz, so the rate is just the cycle
	 * count divided by the window in milliseconds.
	 *
	 * The arithmetic is deliberately kept 32-bit: the kernel links without
	 * libgcc, so a 64-bit divide would pull in __udivdi3 and fail to link.
	 * Both operands fit comfortably - divider * 1000 is about 6e7, and a
	 * 50ms window at 3GHz is about 1.5e8 cycles, well inside 32 bits. */
	const uint32_t interval_ms = (divider * 1000u) / PIT_BASE_FREQ;

	if (!interval_ms || best > 0xFFFFFFFFull)
		return 0;

	return (uint32_t)best / interval_ms;
}

//! Put the CPU in an idle loop until the PIT one-shots.
void pit_sleep(size_t msec)
{
	// Use TIMER2 for sleeping.
	size_t freq = 1000 / msec;
	pit_timer_set(PIT_TIMER2, freq, PIT_OUTMODE_ONESHOT);

	// Toggle the CH2 gate to start the one-shot.
	pit_control_t ctl = pit_control_read();
	ctl.gate_ch2 = 0;
	pit_control_write(ctl);
	ctl.gate_ch2 = 1;
	pit_control_write(ctl);

	// Wait until the output is high (the count finishes).
	/* FIXME: Is volatile needed here? */
	while (pit_control_read().out_ch2 == 0)
		pause();
}
