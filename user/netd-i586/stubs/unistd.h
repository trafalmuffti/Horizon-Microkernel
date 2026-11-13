#pragma once
/*
 * Stub <unistd.h> for wolfIP freestanding build on Horizon.
 * wolfIP includes <unistd.h> unconditionally but only uses POSIX I/O
 * (write/read/close as syscalls) when WOLF_POSIX is defined, which we
 * do not define.  This stub satisfies the include without pulling in
 * any POSIX declarations.
 */
