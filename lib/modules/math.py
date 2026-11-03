"""math (Modules/mathmodule.c): CPython's argument handling, error rules and
algorithms in Python over the libm primitives of runtime/prim/math.c, which
return what libm returns; math_1 and math_2 below turn a NaN or infinite
result into the exception the C code raises."""

import _lucifer

_as_double = _lucifer.m_to_double
_pack, _unpack = _lucifer.float_pack, _lucifer.float_unpack
_fma = _lucifer.m_fma
_ldexp = _lucifer.m_ldexp
_frexp = _lucifer.m_frexp
_fabs = _lucifer.m_fabs
_copysign = _lucifer.m_copysign
_nextafter = _lucifer.m_nextafter

pi = 3.141592653589793
e = 2.718281828459045
tau = 6.283185307179586
inf = float('inf')
nan = float('nan')
_DBL_MIN = 2.2250738585072014e-308
_LOGPI = 1.144729885849400174143427351353058711647


def _to_double(x):
    """PyFloat_AsDouble: a float or int, or what __float__ or __index__ gives."""
    r = _as_double(x)
    if r is None:
        method = getattr(type(x), '__float__', None)
        if method is not None:
            r = method(x)
            if not isinstance(r, float):
                raise TypeError('%s.__float__ returned non-float (type %s)'
                                % (type(x).__name__, type(r).__name__))
        elif hasattr(type(x), '__index__'):
            r = _as_double(x.__index__())
        else:
            raise TypeError('must be real number, not %s' % type(x).__name__)
    return r


def _isnan(x):
    return x != x


def _isfinite(x):
    return x - x == 0.0


def _domain(message, x):
    if message is None:
        raise ValueError('math domain error')
    raise ValueError(message % (repr(x),))


def _math_1(f, arg, can_overflow, message=None):
    x = _to_double(arg)
    r = f(x)
    if r != r and x == x:
        _domain(message, x)
    if (r == inf or r == -inf) and _isfinite(x):
        if can_overflow:
            raise OverflowError('math range error')
        _domain(message, x)
    return r


def _math_2(f, a, b):
    x, y = _to_double(a), _to_double(b)
    r = f(x, y)
    if r != r:
        if x == x and y == y:
            raise ValueError('math domain error')
    elif (r == inf or r == -inf) and _isfinite(x) and _isfinite(y):
        raise OverflowError('math range error')
    return r


def _unary(name, can_overflow, message, doc):
    f = getattr(_lucifer, 'm_' + name)

    def function(x, /):
        return _math_1(f, x, can_overflow, message)
    function.__name__ = function.__qualname__ = name
    function.__doc__ = doc
    return function


def _binary(name, doc):
    f = getattr(_lucifer, 'm_' + name)

    def function(x, y, /):
        return _math_2(f, x, y)
    function.__name__ = function.__qualname__ = name
    function.__doc__ = doc
    return function


_RANGE = 'expected a number in range from -1 up to 1, got %s'
_FINITE = 'expected a finite input, got %s'

acos = _unary('acos', 0, _RANGE, 'Return the arc cosine (measured in radians) of x.\n\nThe result is between 0 and pi.')
acosh = _unary('acosh', 0, 'expected argument value not less than 1, got %s',
               'Return the inverse hyperbolic cosine of x.')
asin = _unary('asin', 0, _RANGE, 'Return the arc sine (measured in radians) of x.\n\nThe result is between -pi/2 and pi/2.')
asinh = _unary('asinh', 0, None, 'Return the inverse hyperbolic sine of x.')
atan = _unary('atan', 0, None, 'Return the arc tangent (measured in radians) of x.\n\nThe result is between -pi/2 and pi/2.')
atan2 = _binary('atan2', 'Return the arc tangent (measured in radians) of y/x.\n\n'
                'Unlike atan(y/x), the signs of both x and y are considered.')
atanh = _unary('atanh', 0, 'expected a number between -1 and 1, got %s',
               'Return the inverse hyperbolic tangent of x.')
cbrt = _unary('cbrt', 0, None, 'Return the cube root of x.')
copysign = _binary('copysign', 'Return a float with the magnitude (absolute value) of x but the sign of y.\n\n'
                   'On platforms that support signed zeros, copysign(1.0, -0.0)\nreturns -1.0.\n')
cos = _unary('cos', 0, _FINITE, 'Return the cosine of x (measured in radians).')
cosh = _unary('cosh', 1, None, 'Return the hyperbolic cosine of x.')
erf = _unary('erf', 0, None, 'Error function at x.')
erfc = _unary('erfc', 0, None, 'Complementary error function at x.')
exp = _unary('exp', 1, None, 'Return e raised to the power of x.')
exp2 = _unary('exp2', 1, None, 'Return 2 raised to the power of x.')
expm1 = _unary('expm1', 1, None, 'Return exp(x)-1.\n\n'
               'This function avoids the loss of precision involved in the direct evaluation of exp(x)-1 for small x.')
fabs = _unary('fabs', 0, None, 'Return the absolute value of the float x.')
log1p = _unary('log1p', 0, 'expected argument value > -1, got %s',
               'Return the natural logarithm of 1+x (base e).\n\n'
               'The result is computed in a way which is accurate for x near zero.')
sin = _unary('sin', 0, _FINITE, 'Return the sine of x (measured in radians).')
sinh = _unary('sinh', 1, None, 'Return the hyperbolic sine of x.')
sqrt = _unary('sqrt', 0, 'expected a nonnegative input, got %s', 'Return the square root of x.')
tan = _unary('tan', 0, _FINITE, 'Return the tangent of x (measured in radians).')
tanh = _unary('tanh', 0, None, 'Return the hyperbolic tangent of x.')
remainder = _binary('remainder', 'Difference between x and the closest integer multiple of y.\n\n'
                    'Return x - n*y where n*y is the closest integer multiple of y.\n'
                    'In the case where x is exactly halfway between two multiples of\n'
                    'y, the nearest even value of n is used. The result is always exact.')


def fmod(x, y, /):
    """Return fmod(x, y), according to platform C.

x % y may differ."""
    x, y = _to_double(x), _to_double(y)
    if (y == inf or y == -inf) and _isfinite(x):
        return x
    r = _lucifer.m_fmod(x, y)
    if r != r and x == x and y == y:
        raise ValueError('math domain error')
    return r


def hypot(*coordinates):
    """Multidimensional Euclidean distance from the origin to a point.

Roughly equivalent to:
    sqrt(sum(x**2 for x in coordinates))

For a two dimensional point (x, y), gives the hypotenuse
using the Pythagorean theorem:  sqrt(x*x + y*y).

For example, the hypotenuse of a 3/4/5 right triangle is:

    >>> hypot(3.0, 4.0)
    5.0
"""
    return _norm([_fabs(_to_double(c)) for c in coordinates])


def dist(p, q, /):
    """Return the Euclidean distance between two points p and q.

The points should be specified as sequences (or iterables) of
coordinates.  Both inputs must have the same dimension.

Roughly equivalent to:
    sqrt(sum((px - qx) ** 2.0 for px, qx in zip(p, q)))
"""
    if not isinstance(p, tuple):
        p = tuple(p)
    if not isinstance(q, tuple):
        q = tuple(q)
    if len(p) != len(q):
        raise ValueError('both points must have the same number of dimensions')
    return _norm([_fabs(_to_double(a) - _to_double(b)) for a, b in zip(p, q)])


def _dl_mul(x, y):
    z = x * y
    return z, _fma(x, y, -z)


def _dl_fast_sum(a, b):
    x = a + b
    return x, (a - x) + b


def _norm(vec):
    """vector_norm: the Euclidean norm of the absolute values in vec, scaled,
    squared exactly, summed with compensation and corrected after the root."""
    n = len(vec)
    mx = 0.0
    found_nan = False
    for x in vec:
        if x != x:
            found_nan = True
        elif x > mx:
            mx = x
    if mx == inf:
        return mx
    if found_nan:
        return nan
    if mx == 0.0 or n <= 1:
        return mx
    return _vector_norm(vec, mx)


def _vector_norm(vec, mx):
    n = len(vec)
    max_e = _frexp(mx)[1]
    if max_e < -1023:
        return _DBL_MIN * _vector_norm([x / _DBL_MIN for x in vec], mx / _DBL_MIN)
    scale = _ldexp(1.0, -max_e)
    csum = 1.0
    frac1 = frac2 = 0.0
    for x in vec:
        x *= scale
        hi, lo = _dl_mul(x, x)
        csum, slo = _dl_fast_sum(csum, hi)
        frac1 += lo
        frac2 += slo
    h = sqrt(csum - 1.0 + (frac1 + frac2))
    hi, lo = _dl_mul(-h, h)
    csum, slo = _dl_fast_sum(csum, hi)
    frac1 += lo
    frac2 += slo
    x = csum - 1.0 + (frac1 + frac2)
    h += x / (2.0 * h)
    return h / scale


def _fma_error(x, y, z):
    r = _fma(x, y, z)
    if r != r:
        if x == x and y == y and z == z:
            raise ValueError('invalid operation in fma')
    elif (r == inf or r == -inf) and _isfinite(x) and _isfinite(y) and _isfinite(z):
        raise OverflowError('overflow in fma')
    return r


def fma(x, y, z, /):
    """Fused multiply-add operation.

Compute (x * y) + z with a single round."""
    return _fma_error(_to_double(x), _to_double(y), _to_double(z))


def pow(x, y, /):
    """Return x**y (x to the power of y)."""
    x, y = _to_double(x), _to_double(y)
    r = _lucifer.m_pow(x, y)
    if _isfinite(x) and _isfinite(y):
        if r != r:
            raise ValueError('math domain error')
        if r == inf or r == -inf:
            if x == 0.0:
                raise ValueError('math domain error')
            raise OverflowError('math range error')
    return r


def ldexp(x, i, /):
    """Return x * (2**i).

This is essentially the inverse of frexp()."""
    x = _to_double(x)
    if not isinstance(i, int):
        raise TypeError('Expected an int as second argument to ldexp.')
    if x == 0.0 or not _isfinite(x):
        return x
    i = max(-2147483648, min(2147483647, i))
    r = _ldexp(x, i)
    if r == inf or r == -inf:
        raise OverflowError('math range error')
    return r


def frexp(x, /):
    """Return the mantissa and exponent of x, as pair (m, e).

m is a float and e is an int, such that x = m * 2.**e.
If x is 0, m and e are both 0.  Else 0.5 <= abs(m) < 1.0."""
    return _frexp(_to_double(x))


def modf(x, /):
    """Return the fractional and integral parts of x.

Both results carry the sign of x and are floats."""
    return _lucifer.m_modf(_to_double(x))


def degrees(x, /):
    """Convert angle x from radians to degrees."""
    return _to_double(x) * (180.0 / pi)


def radians(x, /):
    """Convert angle x from degrees to radians."""
    return _to_double(x) * (pi / 180.0)


def isfinite(x, /):
    """Return True if x is neither an infinity nor a NaN, and False otherwise."""
    x = _to_double(x)
    return x - x == 0.0


def isnan(x, /):
    """Return True if x is a NaN (not a number), and False otherwise."""
    x = _to_double(x)
    return x != x


def isinf(x, /):
    """Return True if x is a positive or negative infinity, and False otherwise."""
    x = _to_double(x)
    return x == inf or x == -inf


def isclose(a, b, *, rel_tol=1e-09, abs_tol=0.0):
    """Determine whether two floating-point numbers are close in value.

  rel_tol
    maximum difference for being considered "close", relative to the
    magnitude of the input values
  abs_tol
    maximum difference for being considered "close", regardless of the
    magnitude of the input values

Return True if a is close in value to b, and False otherwise.

For the values to be considered close, the difference between them
must be smaller than at least one of the tolerances.

-inf, inf and NaN behave similarly to the IEEE 754 Standard.  That
is, NaN is not close to anything, even itself.  inf and -inf are
only close to themselves."""
    a, b = _to_double(a), _to_double(b)
    rel_tol, abs_tol = _to_double(rel_tol), _to_double(abs_tol)
    if rel_tol < 0.0 or abs_tol < 0.0:
        raise ValueError('tolerances must be non-negative')
    if a == b:
        return True
    if a == inf or a == -inf or b == inf or b == -inf:
        return False
    diff = _fabs(b - a)
    return ((diff <= _fabs(rel_tol * b)) or (diff <= _fabs(rel_tol * a))) or diff <= abs_tol


def _bits(x):
    return int.from_bytes(_pack(x, 8), 'little')


def _from_bits(u):
    return _unpack(u.to_bytes(8, 'little'))


def nextafter(x, y, /, *, steps=None):
    """Return the floating-point value the given number of steps after x towards y.

If steps is not specified or is None, it defaults to 1.

Raises a TypeError, if x or y is not a double, or if steps is not an integer.
Raises ValueError if steps is negative."""
    x, y = _to_double(x), _to_double(y)
    if steps is None:
        return _nextafter(x, y)
    steps = _index(steps)
    if steps < 0:
        raise ValueError('steps must be a non-negative integer')
    steps = min(steps, (1 << 64) - 1)
    if steps == 0 or x != x:
        return x
    if y != y:
        return y
    ux, uy = _bits(x), _bits(y)
    if ux == uy:
        return x
    sign_bit = 1 << 63
    ax, ay = ux & ~sign_bit, uy & ~sign_bit
    if (ux ^ uy) & sign_bit:
        if ax + ay <= steps:
            return y
        if ax < steps:
            return _from_bits((uy & sign_bit) | (steps - ax))
        return _from_bits(ux - steps)
    if ax > ay:
        return _from_bits(ux - steps) if ax - ay >= steps else y
    return _from_bits(ux + steps) if ay - ax >= steps else y


def ulp(x, /):
    """Return the value of the least significant bit of the float x."""
    x = _to_double(x)
    if x != x:
        return x
    x = _fabs(x)
    if x == inf:
        return x
    x2 = _nextafter(x, inf)
    if x2 == inf:
        x2 = _nextafter(x, -inf)
        return x - x2
    return x2 - x


# log

def _long_frexp(a):
    """A positive int as m * 2**e, 0.5 <= m < 1, with m rounded to 53 bits."""
    n = a.bit_length()
    if n <= 53:
        return _ldexp(float(a), -n), n
    shift = n - 53
    m = a >> shift
    rest = a & ((1 << shift) - 1)
    half = 1 << (shift - 1)
    if rest > half or (rest == half and m & 1):
        m += 1
    return _ldexp(float(m), -53), n


def _loghelper(arg, f):
    if isinstance(arg, int):
        if arg <= 0:
            raise ValueError('expected a positive input')
        try:
            x = float(arg)
        except OverflowError:
            m, n = _long_frexp(arg)
            return float(f(m) + f(2.0) * n)
    return _math_1(f, arg, 0, 'expected a positive input, got %s')


def log(x, base=None, /):
    """log(x, [base=math.e])
Return the logarithm of x to the given base.

If the base is not specified, returns the natural logarithm (base e) of x."""
    num = _loghelper(x, _lucifer.m_log)
    if base is None:
        return num
    den = _loghelper(base, _lucifer.m_log)
    if den == 0.0:
        raise ZeroDivisionError('division by zero')
    return num / den


def log2(x, /):
    """Return the base 2 logarithm of x."""
    return _loghelper(x, _lucifer.m_log2)


def log10(x, /):
    """Return the base 10 logarithm of x."""
    return _loghelper(x, _lucifer.m_log10)


# floor, ceil, trunc

def floor(x, /):
    """Return the floor of x as an Integral.

This is the largest integer <= x."""
    if type(x) is float:
        return float.__floor__(x)
    method = getattr(type(x), '__floor__', None)
    if method is not None:
        return method(x)
    return float.__floor__(_to_double(x))


def ceil(x, /):
    """Return the ceiling of x as an Integral.

This is the smallest integer >= x."""
    if type(x) is float:
        return float.__ceil__(x)
    method = getattr(type(x), '__ceil__', None)
    if method is not None:
        return method(x)
    return float.__ceil__(_to_double(x))


def trunc(x, /):
    """Truncates the Real x to the nearest Integral toward 0.

Uses the __trunc__ magic method."""
    if type(x) is float:
        return float.__trunc__(x)
    method = getattr(type(x), '__trunc__', None)
    if method is None:
        raise TypeError("type %s doesn't define __trunc__ method" % type(x).__name__)
    return method(x)


# Integers

def _index(n):
    return n if isinstance(n, int) else n.__index__() if hasattr(type(n), '__index__') else _not_int(n)


def _not_int(n):
    raise TypeError("'%s' object cannot be interpreted as an integer" % type(n).__name__)


def gcd(*integers):
    """Greatest Common Divisor."""
    result = 0
    for n in integers:
        a, b = abs(_index(n)), result
        while b:
            a, b = b, a % b
        result = a
    return result


def lcm(*integers):
    """Least Common Multiple."""
    result = 1
    for n in integers:
        n = abs(_index(n))
        if n == 0:
            result = 0
            continue
        if result == 0:
            continue
        a, b = result, n
        while b:
            a, b = b, a % b
        result = result // a * n
    return result


def isqrt(n, /):
    """Return the integer part of the square root of the input."""
    n = _index(n)
    if n < 0:
        raise ValueError('isqrt() argument must be nonnegative')
    if n == 0:
        return 0
    c = (n.bit_length() - 1) // 2
    a = 1
    d = 0
    for s in reversed(range(c.bit_length())):
        e = d
        d = c >> s
        a = (a << d - e - 1) + (n >> 2 * c - e - d + 1) // a
    return a - (a * a > n)


def _odd_product(start, stop):
    """The product of the odd integers in range(start, stop, 2), start and stop odd."""
    if stop - start <= 16:
        total = start
        for j in range(start + 2, stop, 2):
            total *= j
        return total
    middle = (start + (stop - start) // 2) | 1
    return _odd_product(start, middle) * _odd_product(middle, stop)


def factorial(n, /):
    """Find n!."""
    n = _index(n)
    if n < 0:
        raise ValueError('factorial() not defined for negative values')
    if n > (1 << 63) - 1:
        raise OverflowError('factorial() argument should not exceed %d' % ((1 << 63) - 1))
    if n < 2:
        return 1
    inner = outer = 1
    upper = 3
    for i in range(n.bit_length() - 2, -1, -1):
        v = n >> i
        if v <= 2:
            continue
        lower = upper
        upper = (v + 1) | 1
        inner *= _odd_product(lower, upper)
        outer *= inner
    return outer << (n - n.bit_count())


def comb(n, k, /):
    """Number of ways to choose k items from n items without repetition and without order."""
    n, k = _index(n), _index(k)
    if n < 0:
        raise ValueError('n must be a non-negative integer')
    if k < 0:
        raise ValueError('k must be a non-negative integer')
    if k > n:
        return 0
    k = min(k, n - k)
    result = 1
    for i in range(1, k + 1):
        result = result * (n - k + i) // i
    return result


def perm(n, k=None, /):
    """Number of ways to choose k items from n items without repetition and with order."""
    if k is None:
        return factorial(n)
    n, k = _index(n), _index(k)
    if n < 0:
        raise ValueError('n must be a non-negative integer')
    if k < 0:
        raise ValueError('k must be a non-negative integer')
    if k > n:
        return 0
    result = 1
    for i in range(n - k + 1, n + 1):
        result *= i
    return result


# Sums and products

def fsum(seq, /):
    """Return an accurate floating-point sum of values in the iterable seq.

Assumes IEEE-754 floating-point arithmetic."""
    partials = []
    special_sum = inf_sum = 0.0
    for item in seq:
        xsave = x = _to_double(item)
        i = 0
        for y in partials:
            if _fabs(x) < _fabs(y):
                x, y = y, x
            hi = x + y
            lo = y - (hi - x)
            if lo != 0.0:
                partials[i] = lo
                i += 1
            x = hi
        del partials[i:]
        if x != 0.0:
            if not _isfinite(x):
                if _isfinite(xsave):
                    raise OverflowError('intermediate overflow in fsum')
                if xsave == inf or xsave == -inf:
                    inf_sum += xsave
                special_sum += xsave
                del partials[:]
            else:
                partials.append(x)
    if special_sum != 0.0:
        if inf_sum != inf_sum:
            raise ValueError('-inf + inf in fsum')
        return special_sum
    n = len(partials)
    hi = 0.0
    if n:
        n -= 1
        hi = partials[n]
        lo = 0.0
        while n > 0:
            x = hi
            n -= 1
            y = partials[n]
            hi = x + y
            lo = y - (hi - x)
            if lo != 0.0:
                break
        if n > 0 and ((lo < 0.0 and partials[n - 1] < 0.0) or (lo > 0.0 and partials[n - 1] > 0.0)):
            y = lo * 2.0
            x = hi + y
            if y == x - hi:
                hi = x
    return hi


def prod(iterable, /, *, start=1):
    """Calculate the product of all the elements in the input iterable.

The default start value for the product is 1.

When the iterable is empty, return the start value.  This function is
intended specifically for use with numeric values and may reject
non-numeric types."""
    result = start
    for item in iterable:
        result = result * item
    return result


def sumprod(p, q, /):
    """Return the sum of products of values from two iterables p and q.

Roughly equivalent to:

    sum(map(operator.mul, p, q, strict=True))

For float and mixed int/float inputs, the intermediate products
and sums are computed with extended precision."""
    p, q = tuple(p), tuple(q)
    if len(p) != len(q):
        raise ValueError('Inputs are not the same length')
    if all(type(a) is float or type(a) is int for a in p + q) and any(type(a) is float for a in p + q):
        terms = []
        for a, b in zip(p, q):
            a, b = _to_double(a), _to_double(b)
            hi = a * b
            terms.append(hi)
            terms.append(_fma(a, b, -hi) if _isfinite(hi) else 0.0)
        try:
            return fsum(terms)
        except (OverflowError, ValueError):
            total = 0.0
            for a, b in zip(p, q):
                total += _to_double(a) * _to_double(b)
            return total
    total = 0
    for a, b in zip(p, q):
        total = total + a * b
    return total


# The gamma functions (m_tgamma, m_lgamma)

_LANCZOS_G = 6.024680040776729583740234375
_LANCZOS_G_MINUS_HALF = 5.524680040776729583740234375
_LANCZOS_NUM = (
    23531376880.410759688572007674451636754734846804940,
    42919803642.649098768957899047001988850926355848959,
    35711959237.355668049440185451547166705960488635843,
    17921034426.037209699919755754458931112671403265390,
    6039542586.3520280050642916443072979210699388420708,
    1439720407.3117216736632230727949123939715485786772,
    248874557.86205415651146038641322942321632125127801,
    31426415.585400194380614231628318205362874684987640,
    2876370.6289353724412254090516208496135991145378768,
    186056.26539522349504029498971604569928220784236328,
    8071.6720023658162106380029022722506138218516325024,
    210.82427775157934587250973392071336271166969580291,
    2.5066282746310002701649081771338373386264310793408)
_LANCZOS_DEN = (0.0, 39916800.0, 120543840.0, 150917976.0, 105258076.0, 45995730.0,
                13339535.0, 2637558.0, 357423.0, 32670.0, 1925.0, 66.0, 1.0)
_GAMMA_INTEGRAL = (
    1.0, 1.0, 2.0, 6.0, 24.0, 120.0, 720.0, 5040.0, 40320.0, 362880.0,
    3628800.0, 39916800.0, 479001600.0, 6227020800.0, 87178291200.0,
    1307674368000.0, 20922789888000.0, 355687428096000.0,
    6402373705728000.0, 121645100408832000.0, 2432902008176640000.0,
    51090942171709440000.0, 1124000727777607680000.0)
_EDOM, _ERANGE = 1, 2


def _sinpi(x):
    y = _lucifer.m_fmod(_fabs(x), 2.0)
    n = int(round(2.0 * y))
    if n == 0:
        r = sin(pi * y)
    elif n == 1:
        r = cos(pi * (y - 0.5))
    elif n == 2:
        r = sin(pi * (1.0 - y))
    elif n == 3:
        r = -cos(pi * (y - 1.5))
    else:
        r = sin(pi * (y - 2.0))
    return _copysign(1.0, x) * r


def _lanczos_sum(x):
    num = den = 0.0
    if x < 5.0:
        for i in range(12, -1, -1):
            num = num * x + _LANCZOS_NUM[i]
            den = den * x + _LANCZOS_DEN[i]
    else:
        for i in range(13):
            num = num / x + _LANCZOS_NUM[i]
            den = den / x + _LANCZOS_DEN[i]
    return num / den


def _tgamma(x):
    """(value, errno) of CPython's m_tgamma."""
    if not _isfinite(x):
        if x != x or x > 0.0:
            return x, 0
        return nan, _EDOM
    if x == 0.0:
        return _copysign(inf, x), _EDOM
    if x == float.__floor__(x):
        if x < 0.0:
            return nan, _EDOM
        if x <= 23:
            return _GAMMA_INTEGRAL[int(x) - 1], 0
    absx = _fabs(x)
    if absx < 1e-20:
        r = 1.0 / x
        return r, _ERANGE if r == inf or r == -inf else 0
    if absx > 200.0:
        if x < 0.0:
            return 0.0 / _sinpi(x), 0
        return inf, _ERANGE
    y = absx + _LANCZOS_G_MINUS_HALF
    if absx > _LANCZOS_G_MINUS_HALF:
        z = (y - absx) - _LANCZOS_G_MINUS_HALF
    else:
        z = (y - _LANCZOS_G_MINUS_HALF) - absx
    z = z * _LANCZOS_G / y
    pw = _lucifer.m_pow
    if x < 0.0:
        r = -pi / _sinpi(absx) / absx * _lucifer.m_exp(y) / _lanczos_sum(absx)
        r -= z * r
        if absx < 140.0:
            r /= pw(y, absx - 0.5)
        else:
            sqrtpow = pw(y, absx / 2.0 - 0.25)
            r /= sqrtpow
            r /= sqrtpow
    else:
        r = _lanczos_sum(absx) / _lucifer.m_exp(y)
        r += z * r
        if absx < 140.0:
            r *= pw(y, absx - 0.5)
        else:
            sqrtpow = pw(y, absx / 2.0 - 0.25)
            r *= sqrtpow
            r *= sqrtpow
    return r, _ERANGE if r == inf or r == -inf else 0


def _lgamma(x):
    """(value, errno) of CPython's m_lgamma."""
    if not _isfinite(x):
        return (x if x != x else inf), 0
    if x == float.__floor__(x) and x <= 2.0:
        if x <= 0.0:
            return inf, _EDOM
        return 0.0, 0
    absx = _fabs(x)
    log_ = _lucifer.m_log
    if absx < 1e-20:
        return -log_(absx), 0
    r = log_(_lanczos_sum(absx)) - _LANCZOS_G
    r += (absx - 0.5) * (log_(absx + _LANCZOS_G - 0.5) - 1)
    if x < 0.0:
        r = _LOGPI - log_(_fabs(_sinpi(absx))) - log_(absx) - r
    return r, _ERANGE if r == inf or r == -inf else 0


def _math_1a(f, arg, message):
    x = _to_double(arg)
    r, err = f(x)
    if err == _EDOM:
        _domain(message, x)
    if err == _ERANGE and _fabs(r) >= 1.5:
        raise OverflowError('math range error')
    return r


def gamma(x, /):
    """Gamma function at x."""
    return _math_1a(_tgamma, x, 'expected a noninteger or positive integer, got %s')


def lgamma(x, /):
    """Natural logarithm of absolute value of Gamma function at x."""
    return _math_1a(_lgamma, x, 'expected a noninteger or positive integer, got %s')
