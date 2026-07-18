/* See pd_libm_priv.h for background. */
#include <math.h>
#include "pd_libm_priv.h"

/*
 * atan(x) via repeated tangent half-angle reduction:
 *   atan(x) = 2*atan(x / (1 + sqrt(1+x^2)))
 * which roughly halves |x| each step. After enough steps x is tiny and
 * atan(x) ~= x - x^3/3 + x^5/5 - ... converges in a handful of terms.
 */
double
pd_atan(double x)
{
	double sign = 1.0;
	double scale = 1.0;
	int i;
	double x2, sum, term;
	int negate = 0;

	if (isnan(x)) {
		return x;
	}
	if (x < 0.0) {
		negate = 1;
		x = -x;
	}
	if (x > 1.0) {
		/* atan(x) = pi/2 - atan(1/x) for x > 0 */
		double r = PD_PI_2 - pd_atan(1.0 / x);
		return negate ? -r : r;
	}

	for (i = 0; i < 8 && x > 1e-8; i++) {
		x = x / (1.0 + __builtin_sqrt(1.0 + x * x));
		scale *= 2.0;
	}

	x2 = x * x;
	term = x;
	sum = x;
	sign = -1.0;
	for (i = 1; i <= 9; i++) {
		term *= x2;
		sum += sign * term / (2 * i + 1);
		sign = -sign;
	}

	sum *= scale;
	return negate ? -sum : sum;
}

double
atan(double x)
{
	return pd_atan(x);
}

double
atan2(double y, double x)
{
	if (x > 0.0) {
		return pd_atan(y / x);
	}
	if (x < 0.0) {
		if (y >= 0.0) {
			return pd_atan(y / x) + PD_PI;
		}
		return pd_atan(y / x) - PD_PI;
	}
	/* x == 0 */
	if (y > 0.0) {
		return PD_PI_2;
	}
	if (y < 0.0) {
		return -PD_PI_2;
	}
	return 0.0; /* atan2(0,0) */
}

double
asin(double x)
{
	if (x >= 1.0) {
		return PD_PI_2;
	}
	if (x <= -1.0) {
		return -PD_PI_2;
	}
	return pd_atan(x / __builtin_sqrt(1.0 - x * x));
}

double
acos(double x)
{
	return PD_PI_2 - asin(x);
}

/* Odd-order Taylor kernel for |y| <= pi/4, accurate to ~1e-16. */
static double
kernel_sin(double y)
{
	double y2 = y * y;
	double term = y;
	double sum = y;
	int k;

	for (k = 1; k <= 7; k++) {
		term *= -y2 / ((2 * k) * (2 * k + 1));
		sum += term;
	}
	return sum;
}

static double
kernel_cos(double y)
{
	double y2 = y * y;
	double term = 1.0;
	double sum = 1.0;
	int k;

	for (k = 1; k <= 7; k++) {
		term *= -y2 / ((2 * k - 1) * (2 * k));
		sum += term;
	}
	return sum;
}

/* Reduce x to r in (-pi,pi], then to y in [-pi/4,pi/4] plus a quadrant. */
static void
reduce_and_split(double x, double *out_y, int *out_q)
{
	double r;
	long long k;
	int q;

	if (!isfinite(x)) {
		*out_y = 0.0;
		*out_q = 0;
		return;
	}

	/* Best-effort chunked reduction for pathologically large arguments;
	 * precision degrades but callers get a finite, bounded result and we
	 * never overflow the (long long) cast below. */
	while (x > 1e15 || x < -1e15) {
		long long chunk = (long long)(x / 1e15);
		x -= (double)chunk * 1e15;
	}

	k = (long long)(x / PD_2PI);
	r = x - (double)k * PD_2PI;
	while (r > PD_PI) {
		r -= PD_2PI;
	}
	while (r <= -PD_PI) {
		r += PD_2PI;
	}

	q = (int)(r >= 0.0 ? (r / PD_PI_2 + 0.5) : (r / PD_PI_2 - 0.5));
	*out_y = r - q * PD_PI_2;
	*out_q = ((q % 4) + 4) % 4;
}

double
sin(double x)
{
	double y;
	int q;

	reduce_and_split(x, &y, &q);
	switch (q) {
	case 0: return kernel_sin(y);
	case 1: return kernel_cos(y);
	case 2: return -kernel_sin(y);
	default: return -kernel_cos(y);
	}
}

double
cos(double x)
{
	double y;
	int q;

	reduce_and_split(x, &y, &q);
	switch (q) {
	case 0: return kernel_cos(y);
	case 1: return -kernel_sin(y);
	case 2: return -kernel_cos(y);
	default: return kernel_sin(y);
	}
}

/*
 * __sincos_stret / __sincosf_stret: clang emits calls to these when it can
 * fuse a sin(x)+cos(x) pair (i3's rendering code does). The "stret" ABI on
 * x86_64 is just a small-struct return - two doubles come back in
 * xmm0/xmm1, two floats packed in xmm0 - which plain C struct returns
 * already produce. Struct types come from math.h (struct __float2 /
 * __double2). The __sincospi* variants compute sin/cos of pi*x.
 */
struct __double2
__sincos_stret(double x)
{
	struct __double2 r;
	r.__sinval = sin(x);
	r.__cosval = cos(x);
	return r;
}

struct __float2
__sincosf_stret(float x)
{
	struct __float2 r;
	r.__sinval = (float)sin((double)x);
	r.__cosval = (float)cos((double)x);
	return r;
}

struct __double2
__sincospi_stret(double x)
{
	struct __double2 r;
	r.__sinval = sin(x * M_PI);
	r.__cosval = cos(x * M_PI);
	return r;
}

struct __float2
__sincospif_stret(float x)
{
	struct __float2 r;
	r.__sinval = (float)sin((double)x * M_PI);
	r.__cosval = (float)cos((double)x * M_PI);
	return r;
}
