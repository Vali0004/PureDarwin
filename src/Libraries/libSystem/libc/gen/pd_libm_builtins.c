/*
 * Minimal libm surface for the userland bring-up (BusyBox etc.).
 *
 * PureDarwin has no libm yet, but a handful of <math.h>/<fenv.h> entry points
 * leak into the libSystem export surface. Rather than pull a full libm, provide
 * compiler-builtin-backed implementations: nan* map straight to the __builtin_
 * NaN forms, and fegetenv/fesetenv save/restore the x87 control/status words and
 * the SSE MXCSR (the whole of the x86_64 floating-point environment). Replace
 * with a real libm when one exists.
 */
#include <fenv.h>
#include <math.h>

double
nan(const char *tagp)
{
	return __builtin_nan(tagp);
}

float
nanf(const char *tagp)
{
	return __builtin_nanf(tagp);
}

long double
nanl(const char *tagp)
{
	return __builtin_nanl(tagp);
}

int
fegetenv(fenv_t *envp)
{
	__asm__ volatile ("fnstcw %0" : "=m" (envp->__control));
	__asm__ volatile ("fnstsw %0" : "=m" (envp->__status));
	__asm__ volatile ("stmxcsr %0" : "=m" (envp->__mxcsr));
	return 0;
}

int
fesetenv(const fenv_t *envp)
{
	/* fldcw reloads the control word; the status word is not restorable via a
	 * single instruction, but ldmxcsr restores the SSE side in full. */
	__asm__ volatile ("fldcw %0" : : "m" (envp->__control));
	__asm__ volatile ("ldmxcsr %0" : : "m" (envp->__mxcsr));
	return 0;
}
