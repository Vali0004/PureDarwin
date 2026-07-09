/*
 * Minimal dyld private declarations needed by libc's printf core.
 */
#ifndef _PUREDARWIN_MACH_O_DYLD_PRIV_H_
#define _PUREDARWIN_MACH_O_DYLD_PRIV_H_

#include <stdbool.h>
#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

extern bool _dyld_is_memory_immutable(const void *addr, size_t length);

#ifdef __cplusplus
}
#endif

#endif /* _PUREDARWIN_MACH_O_DYLD_PRIV_H_ */
