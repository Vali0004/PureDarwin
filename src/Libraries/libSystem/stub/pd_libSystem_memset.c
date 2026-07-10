#include <stddef.h>

extern void *_platform_memset(void *dst, int value, size_t length);

__attribute__((visibility("default")))
void *
memset(void *dst, int value, size_t length)
{
	return _platform_memset(dst, value, length);
}
