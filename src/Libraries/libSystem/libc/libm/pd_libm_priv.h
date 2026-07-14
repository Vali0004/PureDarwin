/*
 * Shared internals for PureDarwin's from-scratch libm.
 *
 * PureDarwin has no vendored Libm (Apple closed-sourced it after 10.7, and
 * this bootstrap has no FreeBSD msun either), so these are real, derived
 * implementations - not stubs - using standard reduction techniques
 * (tangent half-angle for atan, exponent/mantissa split + Newton for cbrt,
 * atanh-series for log, Taylor for exp) rather than table-driven fdlibm
 * bit-exactness. Good to ~1e-15 relative error for practical inputs; not
 * ULP-correct.
 */
#ifndef PD_LIBM_PRIV_H
#define PD_LIBM_PRIV_H

#include <stdint.h>

#define PD_PI     3.14159265358979323846
#define PD_PI_2   1.57079632679489661923
#define PD_PI_4   0.78539816339744830962
#define PD_2PI    6.28318530717958647693
#define PD_LN2    0.69314718055994530942

/* IEEE754 double decomposition without depending on frexp/ldexp being linked. */
static inline double
pd_ldexp_i(double m, int e)
{
	union { double d; uint64_t u; } v;
	int be;

	if (m == 0.0 || e == 0) {
		return m;
	}
	v.d = m;
	be = (int)((v.u >> 52) & 0x7ff);
	if (be == 0 || be == 0x7ff) {
		/* subnormal/zero or inf/nan: fall back to repeated doubling */
		double r = m;
		int i;
		if (e > 0) {
			for (i = 0; i < e; i++) r *= 2.0;
		} else {
			for (i = 0; i < -e; i++) r *= 0.5;
		}
		return r;
	}
	be += e;
	if (be <= 0 || be >= 0x7ff) {
		/* over/underflow: fall back to repeated doubling for correctness */
		double r = m;
		int i;
		if (e > 0) {
			for (i = 0; i < e; i++) r *= 2.0;
		} else {
			for (i = 0; i < -e; i++) r *= 0.5;
		}
		return r;
	}
	v.u = (v.u & ~((uint64_t)0x7ff << 52)) | ((uint64_t)be << 52);
	return v.d;
}

/* Split x = m * 2^e with |m| in [1,2) (or x==0). */
static inline double
pd_frexp_i(double x, int *e)
{
	union { double d; uint64_t u; } v;
	int be;

	if (x == 0.0) {
		*e = 0;
		return 0.0;
	}
	v.d = x;
	be = (int)((v.u >> 52) & 0x7ff);
	if (be == 0) {
		/* subnormal: scale up then redo */
		v.d = x * 4503599627370496.0; /* 2^52 */
		be = (int)((v.u >> 52) & 0x7ff);
		*e = be - 1023 - 52;
	} else if (be == 0x7ff) {
		*e = 0;
		return x; /* inf/nan */
	} else {
		*e = be - 1023;
	}
	v.u = (v.u & ~((uint64_t)0x7ff << 52)) | ((uint64_t)1023 << 52);
	return v.d;
}

double pd_atan(double x);
double pd_log(double x);
double pd_exp(double x);

#endif
