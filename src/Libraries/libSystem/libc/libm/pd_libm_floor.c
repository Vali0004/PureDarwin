/*
 * pd_libm_floor.c - floor/ceil/trunc/round for the from-scratch mini-libm
 * (see pd_libm_trig.c/pd_libm_pow.c). IEEE-754 double bit manipulation,
 * the classic fdlibm approach: for |x| < 2^52 clear the fractional mantissa
 * bits and adjust by 1.0 depending on sign/direction; for |x| >= 2^52 the
 * value is already integral.
 */
#include <stdint.h>

static inline uint64_t
d2bits(double x)
{
	union { double d; uint64_t u; } c = { .d = x };
	return c.u;
}

static inline double
bits2d(uint64_t u)
{
	union { uint64_t u; double d; } c = { .u = u };
	return c.d;
}

double
trunc(double x)
{
	uint64_t u = d2bits(x);
	int exp = (int)((u >> 52) & 0x7ff) - 1023;

	if (exp < 0) {
		/* |x| < 1: keep only the sign */
		return bits2d(u & 0x8000000000000000ULL);
	}
	if (exp >= 52) {
		/* already integral (or inf/nan) */
		return x;
	}
	u &= ~(0x000fffffffffffffULL >> exp);
	return bits2d(u);
}

double
floor(double x)
{
	double t = trunc(x);

	if (t != x && d2bits(x) >> 63) {
		return t - 1.0;
	}
	return t;
}

double
ceil(double x)
{
	double t = trunc(x);

	if (t != x && !(d2bits(x) >> 63)) {
		return t + 1.0;
	}
	return t;
}

double
round(double x)
{
	if (d2bits(x) >> 63) {
		return -floor(-x + 0.5);
	}
	return floor(x + 0.5);
}

float
truncf(float x)
{
	return (float)trunc((double)x);
}

float
floorf(float x)
{
	return (float)floor((double)x);
}

float
ceilf(float x)
{
	return (float)ceil((double)x);
}

float
roundf(float x)
{
	return (float)round((double)x);
}
