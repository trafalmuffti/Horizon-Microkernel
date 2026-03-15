#pragma once
/*
 * Stub <stdlib.h> for wolfIP freestanding build on Horizon.
 * wolfIP includes <stdlib.h> but does not call any stdlib functions
 * in non-POSIX builds.  We redirect to libh's malloc header for the
 * handful of types that users of this header might expect.
 */
#include <malloc.h>
