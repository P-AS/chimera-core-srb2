/* detmath.c - the C library's inexact math, answered the same in both builds
 * (platform/detmath.h); the link wraps each call:
 *
 *   hypot          SRB2's slopes (p_slopes.c): play
 *   sincos         its renderer (r_plane.c, r_things.c; gcc's pairing of a
 *                  sin and a cos of one angle): the picture
 *   sin, cos, acos, atan, exp, log
 *                  libvorbis's tables (mdct, window, floor0's LSP): the sound
 *   pow            GME's equalizer and filters (fractional powers), libvorbis's
 *                  codebook, and SRB2's Lua (luai_numpow: integer powers of
 *                  integers, which must stay exact - an integer exponent is
 *                  repeated squaring, exact wherever the result fits a double,
 *                  as the C libraries' correctly rounded results are)
 *
 * sqrt, floor, fmod, ldexp, frexp, modf, round and trunc are exact everywhere. */
#define _GNU_SOURCE
#include "detmath.h"

double __wrap_sin(double x) { return dm_sin(x); }
double __wrap_cos(double x) { return dm_cos(x); }
double __wrap_acos(double x) { return DM_PI / 2.0 - dm_asin(x); }
double __wrap_atan(double x) { return dm_atan(x); }
double __wrap_exp(double x) { return dm_exp(x); }
double __wrap_log(double x) { return dm_log(x); }
double __wrap_hypot(double x, double y) { return dm_hypot(x, y); }
double __wrap_pow(double x, double y)
{
	if (y == floor(y) && fabs(y) <= 4096.0 && !isinf(x) && x == x)
	{
		double r = 1.0, b = x;
		for (unsigned long long n = (unsigned long long)fabs(y); n; n >>= 1)
		{
			if (n & 1)
				r *= b;
			b *= b;
		}
		return y < 0.0 ? 1.0 / r : r;
	}
	return dm_pow(x, y);
}

void __wrap_sincos(double x, double *s, double *c)
{
	*s = dm_sin(x);
	*c = dm_cos(x);
}
