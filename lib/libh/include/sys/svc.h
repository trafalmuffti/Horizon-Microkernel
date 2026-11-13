#ifndef _SYS_SVC_H
#define _SYS_SVC_H

#include <horizon/svc.h>
#include <horizon/types.h>
#include <stdint.h>

long svcown(svc_t sid);
ipcport_t svcid(svc_t sid);

/* Rate of the CPU timestamp counter in kHz, measured by the kernel at boot.
 * Returns 0 if unavailable. Userspace has no other time reference, so this
 * is what turns a TSC delta into real time. */
uint32_t tscfreq();

#endif
