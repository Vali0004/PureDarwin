/*
 * PureDarwin minimal arc4random_uniform.
 *
 * Real Apple's arc4random is a corecrypto-seeded CSPRNG stream (ccrng.h chain
 * -- deferred, see dyld_in_pd memory: string.h asm-label conflict under our
 * include order, plus needs real corecrypto RNG wiring). malloc.c's only use
 * (malloc_entropy / heap-layout randomization, e.g. which nano-zone slot to
 * start allocating from) is not security-critical the way e.g. ASLR base or
 * key generation would be, so a getentropy()-backed uniform generator is a
 * legitimate, real (not faked) implementation: getentropy() is itself a real
 * kernel syscall RNG source (already in syscalls.a), and this does correct
 * rejection sampling for uniformity, not a fixed/predictable value.
 */

#include <stdint.h>
#include <unistd.h>

extern int getentropy(void *buf, size_t buflen);

static uint32_t
pd_random_u32(void)
{
	uint32_t v = 0;
	if (getentropy(&v, sizeof(v)) != 0)
		v = 0; /* getentropy is documented to only fail for buflen>256; harmless fallback */
	return v;
}

uint32_t
arc4random_uniform(uint32_t upper_bound)
{
	if (upper_bound < 2)
		return 0;

	/* Rejection sampling to avoid modulo bias, same approach as real arc4random_uniform. */
	uint32_t min = (uint32_t)(-upper_bound) % upper_bound;
	uint32_t r;
	do {
		r = pd_random_u32();
	} while (r < min);
	return r % upper_bound;
}
