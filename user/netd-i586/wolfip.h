#ifndef WOLFIP_HORIZON_WRAPPER_H
#define WOLFIP_HORIZON_WRAPPER_H

/*
 * Horizon-specific wrapper for wolfip.h.
 * Includes the real header then silences LOG output, which would otherwise
 * flood the console since wolfip.h unconditionally defines DEBUG.
 */
#include "../../lib/wolfip/wolfip.h"

/* Override LOG to be a no-op for Horizon builds */
#undef LOG
#define LOG(fmt, ...) do {} while (0)

#endif /* WOLFIP_HORIZON_WRAPPER_H */
