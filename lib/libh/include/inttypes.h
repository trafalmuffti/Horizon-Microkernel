#ifndef _INTTYPES_H
#define _INTTYPES_H

#include <features.h>
#include <limits.h>

typedef signed   char int8_t;
typedef unsigned char uint8_t;

typedef short int16_t;
typedef unsigned short uint16_t;

typedef int int32_t;
typedef unsigned int uint32_t;

#if (LONG_BIT == 32)
typedef          long long int64_t;
typedef unsigned long long uint64_t;
#else
typedef          long int64_t;
typedef unsigned long uint64_t;
#endif

typedef long long intmax_t;
typedef unsigned long long uintmax_t;

/*
 * Derive the pointer-sized types from the compiler's own definitions so they
 * agree with <stddef.h>.  Hardcoding these as long/unsigned long conflicts
 * with __SIZE_TYPE__ / __PTRDIFF_TYPE__ (int on i386): C treats long and int
 * as distinct types even at equal width, so any translation unit including
 * both headers failed to compile.
 */
#ifdef __GNUC__
typedef __SIZE_TYPE__    size_t;
typedef __UINTPTR_TYPE__ uintptr_t;

typedef __INTPTR_TYPE__  ssize_t;
typedef __INTPTR_TYPE__  intptr_t;
typedef __PTRDIFF_TYPE__ ptrdiff_t;
#else
typedef unsigned long size_t;
typedef unsigned long uintptr_t;

typedef long ssize_t;
typedef long intptr_t;
typedef long ptrdiff_t;
#endif

#endif
