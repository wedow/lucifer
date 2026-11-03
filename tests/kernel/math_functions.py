# math: results and errors of every function, on special and ordinary arguments.
import math

INF, NAN = float('inf'), float('nan')
ARGS = [0, 1, -1, 2, 0.5, -0.5, 1.5, -2.5, 10, 100, 1e-300, -1e-300, 1e300, 710, -745, 5e-324, 3.14159, 0.0,
        -0.0, INF, -INF, NAN, 2**70, -2**70, 10**400, True, 22.0, 171.5, -170.5, 1e-25, 200.5, 3, 7, 0.1]


def show(f, *args, **kw):
    try:
        r = f(*args, **kw)
    except Exception as e:
        return '%s: %s' % (type(e).__name__, e)
    return repr(r)


for name in ('acos acosh asin asinh atan atanh cbrt cos cosh erf erfc exp exp2 expm1 fabs floor ceil trunc '
             'gamma lgamma log log10 log2 log1p sin sinh sqrt tan tanh degrees radians isfinite isnan isinf '
             'frexp modf ulp factorial isqrt').split():
    f = getattr(math, name)
    print(name, [show(f, a) for a in ARGS])
for name in 'atan2 copysign fmod remainder hypot pow ldexp nextafter fma isclose dist'.split():
    f = getattr(math, name)
    pairs = [(a, b) for a in (0, 1, -1, 2.5, -3.5, 1e300, INF, -INF, NAN, 1e-300, 0.0, -0.0, 7)
             for b in (0, 1, -1, 2, 0.5, -2.5, 1e300, INF, -INF, NAN, 3, -0.0)]
    if name == 'ldexp':
        pairs = [(a, b) for a, b in pairs if isinstance(b, int)] + [(1.5, 2**70), (1.5, -2**70), (1.0, 1024), (1.0, -1080)]
    if name == 'dist':
        pairs = [((a, b), (b, a)) for a, b in pairs[:20]]
    if name == 'fma':
        print(name, [show(f, a, b, 1.0) for a, b in pairs])
    else:
        print(name, [show(f, a, b) for a, b in pairs])
print(show(math.log, 8, 2), show(math.log, 0, 2), show(math.log, 8, 1), show(math.log, 10**400, 10), show(math.log, 2.0, 0.0),
      show(math.log, -1), show(math.log, 0.0), show(math.log2, 2**5000), show(math.log10, 10**500))
print(show(math.hypot), show(math.hypot, 3, 4), show(math.hypot, 1e-320, 1e-320), show(math.hypot, 1, 2, 3, 4, 5, 6),
      show(math.hypot, INF, NAN), show(math.hypot, 1e200, 1e200), show(math.hypot, 'a'))
print(show(math.fsum, [0.1] * 10), show(math.fsum, [1e100, 1.0, -1e100, 1e-100, 1e50, -1.0, -1e50]),
      show(math.fsum, [INF, -INF]), show(math.fsum, [1e308, 1e308]), show(math.fsum, [INF, 1.0]), show(math.fsum, []),
      show(math.fsum, ['x']), show(math.fsum, [2**3000]))
print(show(math.prod, [1, 2, 3, 4]), show(math.prod, [0.5, 4], start=3), show(math.prod, []), show(math.prod, 'ab'))
print(show(math.sumprod, [1, 2, 3], [4, 5, 6]), show(math.sumprod, [0.1, 0.2], [3, 4.0]), show(math.sumprod, [1], [1, 2]),
      show(math.sumprod, [1e308, 1e308, -1e308], [10, 1, 10]))
print(show(math.gcd), show(math.gcd, 12, 18, -6), show(math.gcd, 0, 0), show(math.gcd, 2.0), show(math.gcd, 2**100, 2**80 * 3),
      show(math.lcm), show(math.lcm, 4, 6, 10), show(math.lcm, 0, 5), show(math.lcm, -3, 5), show(math.lcm, 1.5))
print(show(math.comb, 10, 3), show(math.comb, 5, 6), show(math.comb, 100, 50), show(math.comb, -1, 1), show(math.comb, 3, -1),
      show(math.comb, 3.0, 1), show(math.perm, 10, 3), show(math.perm, 5), show(math.perm, 5, 6), show(math.perm, -1), show(math.perm, 10, None))
print(show(math.factorial, 20), show(math.factorial, 21), show(math.factorial, 100), show(math.factorial, -1), show(math.factorial, 5.0),
      show(math.factorial, 2**70), math.factorial(1000) % 10**9)
print([math.isqrt(n) for n in (0, 1, 2, 3, 4, 15, 16, 17, 10**20, 10**20 - 1, 2**200, 2**200 - 1)], show(math.isqrt, -1), show(math.isqrt, 2.0))
print(show(math.nextafter, 1.0, 2.0, steps=3), show(math.nextafter, 0.0, 1.0, steps=2**64), show(math.nextafter, 1.0, -1.0, steps=0),
      show(math.nextafter, -0.0, 0.0, steps=1), show(math.nextafter, 1.0, 0.0, steps=-1), show(math.nextafter, 1.0, 1.0, steps=5),
      show(math.nextafter, 5e-324, -5e-324, steps=1), show(math.nextafter, 5e-324, -5e-324, steps=2), show(math.nextafter, 1, 2, steps=2.0))
print(show(math.isclose, 1.0, 1.0 + 1e-10), show(math.isclose, 1.0, 1.1), show(math.isclose, 1.0, 1.1, rel_tol=0.2),
      show(math.isclose, 0, 1e-12, abs_tol=1e-9), show(math.isclose, 1, 1, rel_tol=-1), show(math.isclose, INF, INF), show(math.isclose, NAN, NAN))
print(math.pi, math.e, math.tau, math.inf, math.nan, show(math.sin, 'x'), show(math.sin, None), show(math.sin, '1'))


class F:
    def __float__(self):
        return 2.5


class I:
    def __index__(self):
        return 3


class Fl:
    def __floor__(self):
        return 'floor'

    def __ceil__(self):
        return 'ceil'

    def __trunc__(self):
        return 'trunc'


print(show(math.sqrt, F()), show(math.sqrt, I()), show(math.floor, F()), show(math.floor, Fl()), show(math.ceil, Fl()),
      show(math.trunc, Fl()), show(math.trunc, F()), show(math.trunc, 2.7), show(math.floor, -2.5), show(math.ceil, -2.5),
      show(math.floor, 10**30), show(math.ceil, 10**30), show(math.ldexp, 1.0, 2.0), show(math.ldexp, 1.0, I()))
