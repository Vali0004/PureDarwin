#include <math.h>

long
lround(double x)
{
	return (long)round(x);
}

long
lroundf(float x)
{
	return (long)round((double)x);
}

double
tan(double x)
{
	/* sin/cos are the polynomial pd_libm_trig implementations */
	return sin(x) / cos(x);
}

float
tanf(float x)
{
	return (float)tan((double)x);
}

double
tanh(double x)
{
	if (x > 20.0)
		return 1.0;
	if (x < -20.0)
		return -1.0;
	{
		double e2 = exp(2.0 * x);
		return (e2 - 1.0) / (e2 + 1.0);
	}
}

float
tanhf(float x)
{
	return (float)tanh((double)x);
}

float
expf(float x)
{
	return (float)exp((double)x);
}

float
hypotf(float x, float y)
{
	return (float)hypot((double)x, (double)y);
}
