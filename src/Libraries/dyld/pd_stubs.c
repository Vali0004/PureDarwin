/*
 * pd_stubs.c - permissive PureDarwin implementations of the Apple
 * source-available policy hooks that dyld calls but for which there is no open
 * implementation (AMFI, libsandbox). These match the declarations in
 * compat-include/{libamfi.h,sandbox/private.h}. PureDarwin has no AMFI or
 * sandbox, so both are fully permissive.
 */
#include <stdint.h>
#include <stdarg.h>
#include <stddef.h>

int NXArgc = 0;
const char **NXArgv = 0;
const char **environ = 0;
const char *__progname = 0;

/* AMFI: grant every dyld capability. */
int amfi_check_dyld_policy_self(uint64_t input_flags, uint64_t *output_flags)
{
	(void)input_flags;
	if (output_flags)
		*output_flags = ~0ull;   /* allow @path, path vars, fallback paths, interposing, ... */
	return 0;
}

/* sandbox: nothing is sandboxed. */
int sandbox_check(int pid, const char *operation, unsigned int type, ...)
{
	(void)pid; (void)operation; (void)type;
	return 0;   /* 0 == allowed / not-in-sandbox */
}

/* voucher_mach_msg_{adopt,revert}: libdispatch's mach-voucher hooks. dyld's
 * mach_msg wrappers reference them, but PureDarwin has no libdispatch voucher
 * machinery; no-op them (adopt returns "no previous voucher"). */
typedef unsigned int mach_voucher_t;
typedef struct mach_msg_header_t mach_msg_header_t;
mach_voucher_t voucher_mach_msg_adopt(mach_msg_header_t *msg)
{
	(void)msg;
	return 0;   /* MACH_VOUCHER_NULL / MACH_PORT_NULL */
}
void voucher_mach_msg_revert(mach_voucher_t voucher)
{
	(void)voucher;
}

#include <stddef.h>
extern void *malloc(size_t);
extern void abort(void);
void *aligned_alloc(size_t alignment, size_t size)
{
	if (alignment > 16)
		abort();   /* dyld's pool only guarantees 16-byte alignment */
	return malloc(size);
}

/* arc4random/arc4random_buf: real getentropy()-backed implementation, same
 * approach as libc's darwin/pd_arc4random.c (real corecrypto-based
 * arc4random.c is deferred -- see that file's comment for why). The
 * previous version here was a fixed-seed xorshift PRNG: deterministic
 * across every boot, not real randomness -- a genuine correctness gap for
 * anything security-adjacent (ASLR-adjacent pointer obfuscation, hash
 * seeding) that dyld uses these for. */
extern int getentropy(void *buf, size_t buflen);

uint32_t arc4random(void)
{
	uint32_t v = 0;
	if (getentropy(&v, sizeof(v)) != 0)
		v = 0;
	return v;
}

void arc4random_buf(void *buf, size_t nbytes)
{
	unsigned char *p = (unsigned char *)buf;
	/* getentropy() only guarantees up to 256 bytes per call. */
	while (nbytes > 0) {
		size_t chunk = nbytes > 256 ? 256 : nbytes;
		if (getentropy(p, chunk) != 0)
			break;
		p += chunk;
		nbytes -= chunk;
	}
}

void arc4random_stir(void) { /* no persistent state to restir */ }
void arc4random_addrandom(unsigned char *data, int datalen)
{
	(void)data; (void)datalen;   /* getentropy() needs no caller-supplied entropy */
}

extern void _ZN4dyld4haltEPKc(const char *msg) __attribute__((noreturn));

void *_Block_copy(const void *block)
{
	(void)block;
	_ZN4dyld4haltEPKc("_Block_copy()");
}

void _Block_release(const void *block)
{
	(void)block;
	_ZN4dyld4haltEPKc("_Block_release()");
}
