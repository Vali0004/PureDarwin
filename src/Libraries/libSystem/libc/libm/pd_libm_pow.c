/* See pd_libm_priv.h for background. */
#include <math.h>
#include "pd_libm_priv.h"

/*
 * log(x) via mantissa/exponent split (pd_frexp_i gives m in [1,2)) plus the
 * atanh series: log(m) = 2*atanh((m-1)/(m+1)) = 2*(t + t^3/3 + t^5/5 + ...)
 * with t = (m-1)/(m+1) in (-1/3, 1/3], converging fast. log(x) = log(m) + e*ln2.
 */
double
pd_log(double x)
{
	int e;
	double m, t, t2, term, sum;
	int k;

	if (x < 0.0 || isnan(x)) {
		return NAN;
	}
	if (x == 0.0) {
		return -INFINITY;
	}
	if (isinf(x)) {
		return x;
	}

	m = pd_frexp_i(x, &e);
	t = (m - 1.0) / (m + 1.0);
	t2 = t * t;
	term = t;
	sum = t;
	for (k = 1; k <= 8; k++) {
		term *= t2;
		sum += term / (2 * k + 1);
	}
	return 2.0 * sum + (double)e * PD_LN2;
}

double
log(double x)
{
	return pd_log(x);
}

double
log2(double x)
{
	return pd_log(x) / PD_LN2;
}

double
log10(double x)
{
	return pd_log(x) / PD_LN10;
}

/*
 * exp(x) via range reduction x = k*ln2 + r, |r| <= ln2/2, then a Taylor
 * series for exp(r) (fast since r is small), scaled back by 2^k.
 */
double
pd_exp(double x)
{
	long long k;
	double r, term, sum;
	int i;

	if (isnan(x)) {
		return x;
	}
	if (x > 709.0) {
		return INFINITY;
	}
	if (x < -745.0) {
		return 0.0;
	}

	k = (long long)(x / PD_LN2 + (x >= 0.0 ? 0.5 : -0.5));
	r = x - (double)k * PD_LN2;

	term = 1.0;
	sum = 1.0;
	for (i = 1; i <= 15; i++) {
		term *= r / i;
		sum += term;
	}
	return pd_ldexp_i(sum, (int)k);
}

double
exp(double x)
{
	return pd_exp(x);
}

double
cbrt(double x)
{
	double sign, m, y;
	int e, e3, r;
	int i;

	if (x == 0.0 || isnan(x) || isinf(x)) {
		return x;
	}

	sign = 1.0;
	if (x < 0.0) {
		sign = -1.0;
		x = -x;
	}

	m = pd_frexp_i(x, &e); /* x = m * 2^e, m in [1,2) */
	e3 = e / 3;
	r = e % 3;
	if (r < 0) {
		r += 3;
		e3 -= 1;
	}
	m = pd_ldexp_i(m, r); /* m now in [1,8) */

	/* Newton's method for y^3 = m, starting near the middle of [1,8). */
	y = 1.75;
	for (i = 0; i < 8; i++) {
		y = y - (y * y * y - m) / (3.0 * y * y);
	}

	return sign * pd_ldexp_i(y, e3);
}

double
hypot(double x, double y)
{
	double ax, ay, t;

	ax = x < 0.0 ? -x : x;
	ay = y < 0.0 ? -y : y;
	if (isinf(ax) || isinf(ay)) {
		return INFINITY;
	}
	if (ax < ay) {
		t = ax;
		ax = ay;
		ay = t;
	}
	if (ax == 0.0) {
		return 0.0;
	}
	t = ay / ax;
	return ax * __builtin_sqrt(1.0 + t * t);
}

double
pow(double x, double y)
{
	long long iy;
	int y_is_int;

	if (y == 0.0) {
		return 1.0;
	}
	if (isnan(x) || isnan(y)) {
		return NAN;
	}
	if (x == 1.0) {
		return 1.0;
	}
	if (x == 0.0) {
		if (y > 0.0) {
			return 0.0;
		}
		return INFINITY;
	}

	iy = (long long)y;
	y_is_int = ((double)iy == y);

	if (x < 0.0) {
		double r;
		if (!y_is_int) {
			return NAN;
		}
		r = pd_exp(y * pd_log(-x));
		return (iy & 1) ? -r : r;
	}

	return pd_exp(y * pd_log(x));
}
