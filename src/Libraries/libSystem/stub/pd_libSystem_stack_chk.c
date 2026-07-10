#include <stdint.h>

/*
 * Minimal stack protector ABI for early PureDarwin userland.
 *
 * Clang emits references to these symbols whenever stack protector is enabled.
 * Real Darwin provides them from libSystem. Use a fixed non-zero guard for now;
 * once early entropy is reliable this can be seeded during libSystem init.
 */
uintptr_t __stack_chk_guard = (uintptr_t)0x2d3f4b5a69788796ULL;

__attribute__((noreturn))
void
__stack_chk_fail(void)
{
	__builtin_trap();
	for (;;) {
	}
}
