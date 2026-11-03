"""float (Objects/floatobject.c, Python/formatter_unicode.c): the methods,
dunders, parsing and formatting of Python's floats over the IEEE kernels of
runtime/prim/float.c. Operands that are not floats or ints answer
NotImplemented, so reflection and the error messages live in operators.py."""

from _lucifer import (
    MISSING, error, str_join, kind, size, static_type, type_tp_name, type_lookup,
    int_as_double, int_from_double, float_add, float_sub, float_mul, float_truediv,
    float_floordiv, float_mod, float_divmod, float_pow, float_neg, float_abs, float_exact,
    float_subtype, float_hash, float_cmp, float_repr, float_format, float_from_text,
    float_integral, float_is_integer, float_round, float_as_ratio, float_hex, float_fromhex,
    int_format, str_len, str_item, str_slice,
)
from _lucifer import _boot, operators, _format

kernel_type = _boot.kernel_type
classmethod_ = _boot.classmethod_
getset = _boot.getset
call_special = _boot.call_special
index = operators.index

__all__ = ('float',)

# float_cmp's op argument: Py_LT .. Py_GE
_LT, _LE, _EQ, _NE, _GT, _GE = 0, 1, 2, 3, 4, 5
# float_format's flags: Py_DTSF_ALT, Py_DTSF_NO_NEG_0 and Py_DTSF_ADD_DOT_0
_ALT, _NO_NEG_0, _ADD_DOT_0 = 4, 8, 2


def _name(x):
    return type_tp_name(type(x))


def _cat(*parts):
    return str_join('', parts)


def _decimal(n):
    return int_format(n, 10, False)


def _from_string(x):
    """PyFloat_FromString."""
    if not (kind(x, 'str') or kind(x, 'bytes')):
        error(TypeError, _cat(
            "float() argument must be a string or a real number, not '", _name(x), "'"))
    r = float_from_text(x)
    if r is MISSING:
        error(ValueError, _cat('could not convert string to float: ', _format.to_repr(x)))
    return r


def to_double(x):
    """PyFloat_AsDouble: a float, through __float__ or __index__."""
    if kind(x, 'float'):
        return x
    f = type_lookup(type(x), '__float__')
    if f is MISSING:
        if type_lookup(type(x), '__index__') is not MISSING:
            return int_as_double(index(x))
        error(TypeError, _cat('must be real number, not ', _name(x)))
    r = call_special(f, x)
    if not kind(r, 'float'):
        error(TypeError, _cat(_name(x), '.__float__ returned non-float (type ', _name(r), ')'))
    return r


def to_float(x):
    """PyNumber_Float: an exact float."""
    if type(x) is float:
        return x
    if type_lookup(type(x), '__float__') is not MISSING or type_lookup(type(x), '__index__') is not MISSING:
        return float_exact(to_double(x))
    return _from_string(x)


def _exact(x):
    return x if type(x) is float else float_exact(x)


# float.__format__

def _group_fraction(digits, sep):
    """The digits after the decimal point in groups of three from the left."""
    out = ''
    i = 0
    n = str_len(digits)
    while i < n:
        if i > 0:
            out = _cat(out, sep)
        out = _cat(out, str_slice(digits, i, i + 3 if i + 3 < n else n))
        i += 3
    return out


def _is_digit(c):
    return c >= '0' and c <= '9'


def _split_number(s):
    """The digits before the decimal point, and the rest: the point, the
    digits after it and the exponent."""
    n = str_len(s)
    i = 0
    while i < n and _is_digit(str_item(s, i)):
        i += 1
    return str_slice(s, 0, i), str_slice(s, i, n)


def format_parts(x, parsed):
    """format_float_internal up to the padding: (sign char, digits, remainder)
    of x under the parsed spec."""
    fill, align, alternate, no_neg_0, sign, width, thousands, precision, frac, ty = parsed
    if precision > 2147483647:
        error(ValueError, 'precision too big')
    flags = 0
    if alternate:
        flags |= _ALT
    if no_neg_0:
        flags |= _NO_NEG_0
    code = ty
    default_precision = 6
    if ty == '':
        flags |= _ADD_DOT_0
        code = 'r'
        default_precision = 0
    if code == 'n':
        code = 'g'
    if precision < 0:
        precision = default_precision
    elif code == 'r':
        code = 'g'
    if code == '%':
        code = 'f'
        x = float_mul(x, 100)
    buf = float_format(x, code, precision, flags)
    if ty == '%':
        buf = _cat(buf, '%')
    sign_char = ''
    if str_item(buf, 0) == '-':
        sign_char = '-'
        buf = str_slice(buf, 1, str_len(buf))
    digits, rest = _split_number(buf)
    if str_len(rest) > 0 and str_item(rest, 0) == '.':
        fdigits, exponent = _split_number(str_slice(rest, 1, str_len(rest)))
        if frac != '' and fdigits != '':
            fdigits = _group_fraction(fdigits, frac)
        rest = _cat('.', fdigits, exponent)
    return sign_char, digits, rest


def format_float(x, spec):
    """float.__format__."""
    if str_len(spec) == 0:
        return _format.to_str(x)
    parsed = _format.parse_spec(x, spec, '', '>')
    fill, align, alternate, no_neg_0, sign, width, thousands, precision, frac, ty = parsed
    if ty != '' and ty not in 'eEfFgGn%':
        _format._unknown_type(ty, x)
    sign_char, digits, rest = format_parts(x, parsed)
    return _format.number_field(sign_char, '', digits, rest, parsed, False)


@kernel_type(static_type('float'))
class float:
    """Convert a string or number to a floating-point number, if possible."""

    def __new__(cls, *args, **kwargs):
        _boot.check_new(cls, float)
        if kwargs:
            error(TypeError, 'float() takes no keyword arguments')
        n = size(args)
        if n > 1:
            error(TypeError, _cat('float expected at most 1 argument, got ', _decimal(n)))
        value = 0.0
        for arg in args:
            value = to_float(arg)
        if cls is float:
            return value
        return float_subtype(cls, value)

    def __repr__(self):
        return float_repr(self)

    def __hash__(self):
        return float_hash(self)

    def __lt__(self, other):
        return float_cmp(self, other, _LT)

    def __le__(self, other):
        return float_cmp(self, other, _LE)

    def __eq__(self, other):
        return float_cmp(self, other, _EQ)

    def __ne__(self, other):
        return float_cmp(self, other, _NE)

    def __gt__(self, other):
        return float_cmp(self, other, _GT)

    def __ge__(self, other):
        return float_cmp(self, other, _GE)

    def __add__(self, other):
        return float_add(self, other)

    def __radd__(self, other):
        return float_add(other, self)

    def __sub__(self, other):
        return float_sub(self, other)

    def __rsub__(self, other):
        return float_sub(other, self)

    def __mul__(self, other):
        return float_mul(self, other)

    def __rmul__(self, other):
        return float_mul(other, self)

    def __truediv__(self, other):
        return float_truediv(self, other)

    def __rtruediv__(self, other):
        return float_truediv(other, self)

    def __floordiv__(self, other):
        return float_floordiv(self, other)

    def __rfloordiv__(self, other):
        return float_floordiv(other, self)

    def __mod__(self, other):
        return float_mod(self, other)

    def __rmod__(self, other):
        return float_mod(other, self)

    def __divmod__(self, other):
        return float_divmod(self, other)

    def __rdivmod__(self, other):
        return float_divmod(other, self)

    def __pow__(self, other, mod=None):
        return float_pow(self, other, mod)

    def __rpow__(self, other, mod=None):
        return float_pow(other, self, mod)

    def __neg__(self):
        return float_neg(self)

    def __pos__(self):
        return _exact(self)

    def __abs__(self):
        return float_abs(self)

    def __bool__(self):
        return float_cmp(self, 0, _NE)

    def __float__(self):
        return _exact(self)

    def __int__(self):
        return int_from_double(self)

    def __trunc__(self):
        """Return the Integral closest to x between 0 and x."""
        return int_from_double(self)

    def __floor__(self):
        """Return the floor as an Integral."""
        return int_from_double(float_integral(self, 0))

    def __ceil__(self):
        """Return the ceiling as an Integral."""
        return int_from_double(float_integral(self, 1))

    def __round__(self, ndigits=None, /):
        """Return the Integral closest to x, rounding half toward even.

When an argument is passed, work like built-in round(x, ndigits)."""
        if ndigits is None:
            return int_from_double(float_integral(self, 2))
        return float_round(self, index(ndigits))

    def conjugate(self):
        """Return self, the complex conjugate of any float."""
        return _exact(self)

    def is_integer(self):
        """Return True if the float is an integer."""
        return float_is_integer(self)

    def as_integer_ratio(self):
        """Return a pair of integers, whose ratio is exactly equal to the original float.

The ratio is in lowest terms and has a positive denominator.  Raise
OverflowError on infinities and a ValueError on NaNs.

>>> (10.0).as_integer_ratio()
(10, 1)
>>> (0.0).as_integer_ratio()
(0, 1)
>>> (-.25).as_integer_ratio()
(-1, 4)"""
        return float_as_ratio(self)

    def hex(self):
        """Return a hexadecimal representation of a floating-point number.

>>> (-0.1).hex()
'-0x1.999999999999ap-4'
>>> 3.14159.hex()
'0x1.921f9f01b866ep+1'"""
        return float_hex(self)

    @classmethod_
    def fromhex(cls, string, /):
        """Create a floating-point number from a hexadecimal string.

>>> float.fromhex('0x1.ffffp10')
2047.984375
>>> float.fromhex('-0x1p-1074')
-5e-324"""
        if not kind(string, 'str'):
            error(TypeError, 'bad argument type for built-in operation')
        value = float_fromhex(string)
        if cls is float:
            return value
        return cls(value)

    @classmethod_
    def from_number(cls, number, /):
        """Convert real number to a floating-point number."""
        if type(number) is float and cls is float:
            return number
        value = to_double(number)
        if cls is float:
            return _exact(value)
        return cls(_exact(value))

    @classmethod_
    def __getformat__(cls, typestr, /):
        """You probably don't want to use this function.

It exists mainly to be used in Python's test suite.

This function returns whichever of 'unknown', 'IEEE, big-endian' or
'IEEE, little-endian' best describes the format of floating-point
numbers used by the C type named by typestr."""
        if not kind(typestr, 'str'):
            error(TypeError, _cat('__getformat__() argument must be str, not ', _name(typestr)))
        if typestr != 'double' and typestr != 'float':
            error(ValueError, "__getformat__() argument 1 must be 'double' or 'float'")
        return 'IEEE, little-endian'

    def __getnewargs__(self):
        return (_exact(self),)

    def __format__(self, format_spec, /):
        """Formats the float according to format_spec."""
        if not kind(format_spec, 'str'):
            error(TypeError, _cat('__format__() argument must be str, not ', _name(format_spec)))
        return format_float(self, format_spec)

    real = getset(_exact)
    imag = getset(lambda self: 0.0)
