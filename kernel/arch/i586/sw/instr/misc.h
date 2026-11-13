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

/*! \file arch/i586/sw/instr/misc.h
 *  \date June 2014
 */

#pragma once

#include <stddef.h>

//! Prevents optimization of tight/idle loops.
static inline void pause()
{
	__asm ("pause" ::: "memory");
}

#include <stdint.h>

//! Read the CPU timestamp counter as a single 64-bit value.
/*! A convenience over rdtsc() in instr/info.h, which returns the halves
 *  separately; cycle deltas are easier to reason about unsplit. */
static inline uint64_t rdtsc64()
{
	uint32_t lo, hi;
	__asm ("rdtsc" : "=a"(lo), "=d"(hi));
	return ((uint64_t)hi << 32) | lo;
}
