#ifndef _STDINT_H
#define _STDINT_H

#include <features.h>
#include <inttypes.h>

typedef int8_t  int_fast8_t;
typedef int64_t int_fast64_t;

typedef int8_t  int_least8_t;
typedef int16_t int_least16_t;
typedef int32_t int_least32_t;
typedef int64_t int_least64_t;

typedef uint8_t  uint_fast8_t;
typedef uint64_t uint_fast64_t;

typedef uint8_t  uint_least8_t;
typedef uint16_t uint_least16_t;
typedef uint32_t uint_least32_t;
typedef uint64_t uint_least64_t;

/*
 * Limits of the exact-width types.
 *
 * C99 7.18.2 requires these alongside the typedefs; without them any
 * conforming source using e.g. UINT32_MAX fails to compile.  The suffixes
 * keep each constant's type matching the typedef it describes (int32_t is
 * int and uint64_t is unsigned long long on this platform, per inttypes.h).
 */
#define INT8_MIN   (-128)
#define INT8_MAX     127
#define UINT8_MAX    255

#define INT16_MIN  (-32768)
#define INT16_MAX    32767
#define UINT16_MAX   65535

#define INT32_MIN  (-2147483647 - 1)
#define INT32_MAX    2147483647
#define UINT32_MAX   4294967295U

#define INT64_MIN  (-9223372036854775807LL - 1)
#define INT64_MAX    9223372036854775807LL
#define UINT64_MAX   18446744073709551615ULL

/* Minimum-width and fastest minimum-width types share the exact widths. */
#define INT_LEAST8_MIN    INT8_MIN
#define INT_LEAST8_MAX    INT8_MAX
#define UINT_LEAST8_MAX   UINT8_MAX
#define INT_LEAST16_MIN   INT16_MIN
#define INT_LEAST16_MAX   INT16_MAX
#define UINT_LEAST16_MAX  UINT16_MAX
#define INT_LEAST32_MIN   INT32_MIN
#define INT_LEAST32_MAX   INT32_MAX
#define UINT_LEAST32_MAX  UINT32_MAX
#define INT_LEAST64_MIN   INT64_MIN
#define INT_LEAST64_MAX   INT64_MAX
#define UINT_LEAST64_MAX  UINT64_MAX

#define INT_FAST8_MIN     INT8_MIN
#define INT_FAST8_MAX     INT8_MAX
#define UINT_FAST8_MAX    UINT8_MAX
#define INT_FAST64_MIN    INT64_MIN
#define INT_FAST64_MAX    INT64_MAX
#define UINT_FAST64_MAX   UINT64_MAX

/* Pointer-sized and greatest-width types. */
#define INTPTR_MIN   INT32_MIN
#define INTPTR_MAX   INT32_MAX
#define UINTPTR_MAX  UINT32_MAX

#define INTMAX_MIN   INT64_MIN
#define INTMAX_MAX   INT64_MAX
#define UINTMAX_MAX  UINT64_MAX

#define PTRDIFF_MIN  INT32_MIN
#define PTRDIFF_MAX  INT32_MAX
#define SIZE_MAX     UINT32_MAX

/* Macros for constructing constants of the exact-width types (C99 7.18.4). */
#define INT8_C(c)    (c)
#define UINT8_C(c)   (c)
#define INT16_C(c)   (c)
#define UINT16_C(c)  (c)
#define INT32_C(c)   (c)
#define UINT32_C(c)  (c ## U)
#define INT64_C(c)   (c ## LL)
#define UINT64_C(c)  (c ## ULL)
#define INTMAX_C(c)  (c ## LL)
#define UINTMAX_C(c) (c ## ULL)

#endif
