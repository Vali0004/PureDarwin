/* PureDarwin compat redirect: xnu's PrivateHeaders machine/cpu_capabilities.h
 * includes <System/i386/cpu_capabilities.h>, but PD's header install places the
 * real file at <i386/cpu_capabilities.h> (no System/ umbrella prefix). Redirect. */
#include <i386/cpu_capabilities.h>
