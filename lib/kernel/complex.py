"""complex (Objects/complexobject.c, Python/formatter_unicode.c): the methods,
dunders, construction, parsing and formatting of Python's complex numbers over
the arithmetic of runtime/prim/complex.c. Operands that are not numbers answer
NotImplemented, so reflection and the error messages live in operators.py."""

from _lucifer import (
    MISSING, error, str_join, kind, size, ns_items, static_type, type_tp_name, type_lookup,
    float_format, float_cmp, complex_add, complex_sub, complex_mul, complex_truediv,
    complex_pow, complex_neg, complex_abs, complex_new, complex_part, complex_hash,
    complex_cmp, complex_repr, complex_from_text, int_format, str_len, str_item, str_slice,
)
from _lucifer import _boot, _format, float

kernel_type = _boot.kernel_type
classmethod_ = _boot.classmethod_
getset = _boot.getset
call_special = _boot.call_special

__all__ = ('complex',)

_EQ, _NE = 2, 3
_ALT, _NO_NEG_0 = 4, 8


def _name(x):
    return type_tp_name(type(x))


def _cat(*parts):
    return str_join('', parts)


def _decimal(n):
    return int_format(n, 10, False)


def _special(x):
    """try_complex_special_method: x.__complex__() or MISSING."""
    f = type_lookup(type(x), '__complex__')
    if f is MISSING:
        return MISSING
    r = call_special(f, x)
    if not kind(r, 'complex'):
        error(TypeError, _cat('__complex__ returned non-complex (type ', _name(r), ')'))
    return r


def _is_real(x):
    return type_lookup(type(x), '__float__') is not MISSING or type_lookup(type(x), '__index__') is not MISSING


def _from_string(cls, x):
    """complex_subtype_from_string."""
    if not kind(x, 'str'):
        error(TypeError, _cat('complex() argument must be a string or a number, not ', _name(x)))
    r = complex_from_text(x)
    if r is None:
        error(ValueError, _cat('could not convert string to complex: ', _format.to_repr(x)))
    if r is MISSING:
        error(ValueError, 'complex() arg is a malformed string')
    return _make(cls, complex_part(r, False), complex_part(r, True))


def _make(cls, re, im):
    return complex_new(cls, re, im)


def _parts(z):
    return complex_part(z, False), complex_part(z, True)


def _from_numbers(cls, r, i):
    """complex_new_impl: real r and imag i (MISSING when absent)."""
    if r is MISSING:
        r = 0
    orig_r = r
    tmp = _special(r)
    if tmp is not MISSING:
        r = tmp
    if not (kind(r, 'complex') or _is_real(r)):
        error(TypeError, _cat("complex() argument 'real' must be a real number, not ", _name(r)))
    if i is not MISSING and not (kind(i, 'complex') or _is_real(i)):
        error(TypeError, _cat("complex() argument 'imag' must be a real number, not ", _name(i)))
    cr_complex = kind(r, 'complex')
    ci_complex = False
    if cr_complex:
        cr_re, cr_im = _parts(r)
    else:
        cr_re = float.to_float(r)
        cr_im = 0.0
    ci_im = 0.0
    if i is MISSING:
        ci_re = cr_im
    elif kind(i, 'complex'):
        ci_re, ci_im = _parts(i)
        ci_complex = True
    else:
        ci_re = float.to_float(i)
    # A complex part is not canonical: fold its other component in.
    if ci_complex:
        cr_re = cr_re - ci_im
    if cr_complex and i is not MISSING:
        ci_re = ci_re + cr_im
    return _make(cls, cr_re, ci_re)


def _new_from_one(cls, arg):
    """actual_complex_new's one positional argument."""
    if type(arg) is complex and cls is complex:
        return arg
    if kind(arg, 'str'):
        return _from_string(cls, arg)
    tmp = _special(arg)
    if tmp is not MISSING:
        return _make(cls, complex_part(tmp, False), complex_part(tmp, True))
    if kind(arg, 'complex'):
        return _make(cls, complex_part(arg, False), complex_part(arg, True))
    if _is_real(arg):
        return _make(cls, float.to_double(arg), 0.0)
    error(TypeError, _cat('complex() argument must be a string or a number, not ', _name(arg)))


def _exact(z):
    return z if type(z) is complex else complex_new(complex, complex_part(z, False), complex_part(z, True))


# complex.__format__

def format_complex(z, spec):
    """complex.__format__ (format_complex_internal)."""
    if str_len(spec) == 0:
        return _format.to_str(z)
    parsed = _format.parse_spec(z, spec, '', '>')
    fill, align, alternate, no_neg_0, sign, width, thousands, precision, frac, ty = parsed
    if ty != '' and ty not in 'eEfFgGn':
        _format._unknown_type(ty, z)
    if precision > 2147483647:
        error(ValueError, 'precision too big')
    if fill == '0':
        error(ValueError, 'Zero padding is not allowed in complex format specifier')
    if align == '=':
        error(ValueError, "'=' alignment flag is not allowed in complex format specifier")
    re, im = _parts(z)
    skip_re = False
    add_parens = False
    if ty == '' and float_cmp(re, 0, _EQ) and float_format(re, 'r', 0, 2) == '0.0':
        skip_re = True
    elif ty == '':
        add_parens = True
    # each part is formatted unpadded: the real part under the requested sign,
    # the imaginary part always signed unless the real part is omitted
    plain = (' ', '<', alternate, no_neg_0, sign, -1, thousands, precision, frac, ty)
    signed = (' ', '<', alternate, no_neg_0, sign if skip_re else '+', -1, thousands, precision, frac, ty)
    body = ''
    if not skip_re:
        body = _unpadded(re, plain)
    body = _cat(body, _unpadded(im, signed), 'j')
    if add_parens:
        body = _cat('(', body, ')')
    pad = width - str_len(body)
    if pad <= 0:
        return body
    if align == '<':
        return _cat(body, _format._pad(fill, pad))
    if align == '^':
        return _cat(_format._pad(fill, pad // 2), body, _format._pad(fill, pad - pad // 2))
    return _cat(_format._pad(fill, pad), body)


def _unpadded(x, parsed):
    sign_char, digits, rest = float.format_parts(x, parsed, False)
    return _format.number_field(sign_char, '', digits, rest, parsed, False)


@kernel_type(static_type('complex'))
class complex:
    """Create a complex number from a string or numbers.

If a string is given, parse it as a complex number.
If a single number is given, convert it to a complex number.
If the 'real' or 'imag' arguments are given, create a complex number
with the specified real and imaginary components."""

    def __new__(cls, /, *args, **kwargs):
        _boot.check_new(cls, complex)
        n = size(args)
        given = n + size(kwargs)
        if given > 2:
            error(TypeError, _cat('complex() takes at most 2 arguments (', _decimal(given), ' given)'))
        r = MISSING
        i = MISSING
        k = 0
        for arg in args:
            if k == 0:
                r = arg
            else:
                i = arg
            k += 1
        if kwargs:
            for key, value in ns_items(kwargs):
                if key == 'real':
                    if n >= 1:
                        error(TypeError, "argument for complex() given by name ('real') and position (1)")
                    r = value
                elif key == 'imag':
                    if n >= 2:
                        error(TypeError, "argument for complex() given by name ('imag') and position (2)")
                    i = value
                else:
                    error(TypeError, _cat("complex() got an unexpected keyword argument '", key, "'"))
        elif i is MISSING:
            if r is MISSING:
                return _make(cls, 0.0, 0.0)
            return _new_from_one(cls, r)
        return _from_numbers(cls, r, i)

    def __repr__(self):
        return complex_repr(self)

    def __hash__(self):
        return complex_hash(self)

    def __lt__(self, other):
        return complex_cmp(self, other, 0)

    def __le__(self, other):
        return complex_cmp(self, other, 1)

    def __eq__(self, other):
        return complex_cmp(self, other, _EQ)

    def __ne__(self, other):
        return complex_cmp(self, other, _NE)

    def __gt__(self, other):
        return complex_cmp(self, other, 4)

    def __ge__(self, other):
        return complex_cmp(self, other, 5)

    def __add__(self, other):
        return complex_add(self, other)

    def __radd__(self, other):
        return complex_add(other, self)

    def __sub__(self, other):
        return complex_sub(self, other)

    def __rsub__(self, other):
        return complex_sub(other, self)

    def __mul__(self, other):
        return complex_mul(self, other)

    def __rmul__(self, other):
        return complex_mul(other, self)

    def __truediv__(self, other):
        return complex_truediv(self, other)

    def __rtruediv__(self, other):
        return complex_truediv(other, self)

    def __pow__(self, other, mod=None):
        return complex_pow(self, other, mod)

    def __rpow__(self, other, mod=None):
        return complex_pow(other, self, mod)

    def __neg__(self):
        return complex_neg(self)

    def __pos__(self):
        return _exact(self)

    def __abs__(self):
        return complex_abs(self)

    def __bool__(self):
        return float_cmp(complex_part(self, False), 0, _NE) or float_cmp(complex_part(self, True), 0, _NE)

    def conjugate(self):
        """Return the complex conjugate of its argument. (3-4j).conjugate() == 3+4j."""
        return complex_new(complex, complex_part(self, False), -complex_part(self, True))

    def __complex__(self):
        """Convert this value to exact type complex."""
        return _exact(self)

    @classmethod_
    def from_number(cls, number, /):
        """Convert number to a complex floating-point number."""
        if type(number) is complex and cls is complex:
            return number
        if kind(number, 'complex'):
            z = number
        else:
            z = _special(number)
            if z is MISSING:
                z = complex_new(complex, float.to_double(number), 0.0)
        if cls is complex:
            return _exact(z)
        return cls(_exact(z))

    def __getnewargs__(self):
        return _parts(self)

    def __format__(self, format_spec, /):
        """Convert to a string according to format_spec."""
        if not kind(format_spec, 'str'):
            error(TypeError, _cat('__format__() argument must be str, not ', _name(format_spec)))
        return format_complex(self, format_spec)

    real = getset(lambda self: complex_part(self, False))
    imag = getset(lambda self: complex_part(self, True))
