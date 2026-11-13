#include <internal/syscall.h>
#include <internal/errno.h>
#include <sys/svc.h>
#include <errno.h>

uint32_t tscfreq()
{
	long res = syscall(SYS_tscfreq);
	if (res <= 0)
	{
		errno = ENOTAVAIL;
		return 0;
	}

	return (uint32_t)res;
}
