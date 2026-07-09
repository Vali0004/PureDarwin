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

/* aligned_alloc: pulled by libc++abi's over-aligned operator new
 * (operator new(size, align_val_t)). dyld supplies its own malloc/free (a pool
 * allocator in dyldNew.cpp) whose blocks are already max_align_t (16-byte)
 * aligned, which covers every alignment dyld's own C++ objects request. Route
 * to it so the result is freeable by dyld's free(). (Alignments > 16 are not
 * exercised during bring-up; guard against them so a future over-aligned type
 * fails loudly rather than silently mis-aligning.) */
#include <stddef.h>
extern void *malloc(size_t);
extern void abort(void);
void *aligned_alloc(size_t alignment, size_t size)
{
	if (alignment > 16)
		abort();   /* dyld's pool only guarantees 16-byte alignment */
	return malloc(size);
}

/* ---- tiny libc leaves for the standalone dyld bring-up ------------------- */

void __chk_fail_overflow(void)
{
	abort();
}

void __chk_fail_overlap(void)
{
	abort();
}

int sysctl(int *name, unsigned int namelen, void *oldp, size_t *oldlenp,
    void *newp, size_t newlen)
{
	(void)name; (void)namelen; (void)newp; (void)newlen;
	if (oldlenp)
		*oldlenp = 0;
	if (oldp)
		return -1;
	return 0;
}

static uint32_t pd_arc4_state = 0x9e3779b9u;
uint32_t arc4random(void)
{
	uint32_t x = pd_arc4_state;
	x ^= x << 13;
	x ^= x >> 17;
	x ^= x << 5;
	pd_arc4_state = x ? x : 0x9e3779b9u;
	return pd_arc4_state;
}

void arc4random_buf(void *buf, size_t nbytes)
{
	unsigned char *p = (unsigned char *)buf;
	for (size_t i = 0; i < nbytes; ++i) {
		if ((i & 3) == 0)
			(void)arc4random();
		p[i] = (unsigned char)(pd_arc4_state >> ((i & 3) * 8));
	}
}

void arc4random_stir(void) { (void)arc4random(); }
void arc4random_addrandom(unsigned char *data, int datalen)
{
	for (int i = 0; i < datalen; ++i)
		pd_arc4_state ^= ((uint32_t)data[i] << ((i & 3) * 8));
}

static void pd_putc(char **out, size_t *remaining, int *count, char c)
{
	if (*remaining > 1) {
		**out = c;
		++*out;
		--*remaining;
	}
	++*count;
}

static void pd_puts(char **out, size_t *remaining, int *count, const char *s)
{
	if (!s)
		s = "(null)";
	while (*s)
		pd_putc(out, remaining, count, *s++);
}

static void pd_putnum(char **out, size_t *remaining, int *count,
    unsigned long long value, unsigned base, int is_signed, int negative)
{
	char buf[32];
	unsigned i = 0;
	if (is_signed && negative) {
		pd_putc(out, remaining, count, '-');
	}
	do {
		unsigned digit = value % base;
		buf[i++] = (char)((digit < 10) ? ('0' + digit) : ('a' + digit - 10));
		value /= base;
	} while (value != 0 && i < sizeof(buf));
	while (i)
		pd_putc(out, remaining, count, buf[--i]);
}

int vsnprintf(char *str, size_t size, const char *fmt, va_list ap)
{
	char *out = str;
	size_t remaining = size;
	int count = 0;

	for (; *fmt; ++fmt) {
		if (*fmt != '%') {
			pd_putc(&out, &remaining, &count, *fmt);
			continue;
		}

		++fmt;
		while (*fmt == 'l' || *fmt == 'z' || *fmt == 't')
			++fmt;

		switch (*fmt) {
		case '%':
			pd_putc(&out, &remaining, &count, '%');
			break;
		case 'c':
			pd_putc(&out, &remaining, &count, va_arg(ap, int));
			break;
		case 's':
			pd_puts(&out, &remaining, &count, va_arg(ap, const char *));
			break;
		case 'p':
			pd_puts(&out, &remaining, &count, "0x");
			pd_putnum(&out, &remaining, &count,
			    (unsigned long long)(uintptr_t)va_arg(ap, void *), 16, 0, 0);
			break;
		case 'd':
		case 'i': {
			long long v = va_arg(ap, int);
			pd_putnum(&out, &remaining, &count,
			    (v < 0) ? (unsigned long long)-v : (unsigned long long)v, 10, 1, v < 0);
			break;
		}
		case 'u':
			pd_putnum(&out, &remaining, &count, va_arg(ap, unsigned int), 10, 0, 0);
			break;
		case 'x':
		case 'X':
			pd_putnum(&out, &remaining, &count, va_arg(ap, unsigned int), 16, 0, 0);
			break;
		default:
			pd_putc(&out, &remaining, &count, '%');
			if (*fmt)
				pd_putc(&out, &remaining, &count, *fmt);
			break;
		}
	}

	if (size != 0)
		*out = '\0';
	return count;
}

int snprintf(char *str, size_t size, const char *fmt, ...)
{
	va_list ap;
	va_start(ap, fmt);
	int result = vsnprintf(str, size, fmt, ap);
	va_end(ap);
	return result;
}

int vsnprintf_l(char *str, size_t size, void *loc, const char *fmt, va_list ap)
{
	(void)loc;
	return vsnprintf(str, size, fmt, ap);
}

int snprintf_l(char *str, size_t size, void *loc, const char *fmt, ...)
{
	(void)loc;
	va_list ap;
	va_start(ap, fmt);
	int result = vsnprintf(str, size, fmt, ap);
	va_end(ap);
	return result;
}

int vfprintf(void *stream, const char *fmt, va_list ap)
{
	(void)stream; (void)fmt; (void)ap;
	return 0;
}

int fflush(void *stream) { (void)stream; return 0; }
int fputc(int c, void *stream) { (void)stream; return c; }
void flockfile(void *stream) { (void)stream; }
void funlockfile(void *stream) { (void)stream; }
