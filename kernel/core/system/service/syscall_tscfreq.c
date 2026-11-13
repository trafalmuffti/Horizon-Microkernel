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

#include <system/syscalls.h>
#include <arch.h>

//! Report the rate of the CPU timestamp counter, in kHz.
/*! Userspace reads the TSC directly to time its own work, but cannot
 *  discover the counter's rate: CPUID does not report it on this baseline,
 *  and the only fixed-rate reference on the machine is the PIT, which the
 *  kernel owns for scheduling. The kernel measures the rate once at boot
 *  and hands out the result here.
 *
 *  Returns 0 if calibration failed, which callers should treat as "no
 *  usable wall-clock scale" rather than as a rate. */
void syscall_tscfreq()
{
	syscall_return_set(arch_tsc_khz());
}
