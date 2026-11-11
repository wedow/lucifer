"""int and bool (Objects/longobject.c, boolobject.c): the methods, dunders and
parsing of Python's integers over the digit-vector primitives of
runtime/prim/int.c. Operands that are not ints answer NotImplemented, so
reflection and the error messages live in operators.py."""

from _lucifer import (
    MISSING, error, str_join, kind, size, ns_items, static_type, type_tp_name, type_lookup,
    int_add, int_sub, int_mul, int_floordiv, int_mod, int_divmod, int_pow, int_truediv,
    int_neg, int_abs, int_invert, int_shift, int_bitwise, int_cmp, int_bit_length,
    int_bit_count, int_hash, int_to_bytes, int_from_bytes, int_format, int_from_text,
    int_as_double, int_subtype, int_is_zero, text_repr, b_check, b_bytes, b_from_ints,
    int_from_double, kernel_twin,
)
from _lucifer import _boot, operators, _format

kernel_type = _boot.kernel_type
classmethod_ = _boot.classmethod_
getset = _boot.getset
call_special = _boot.call_special
index = operators.index
truth = operators.truth

__all__ = ('int', 'bool')


def _exact(x):
    """x as an exact int."""
    return x if type(x) is int else int_add(x, 0)


def _name(x):
    return type_tp_name(type(x))


def _decimal(n):
    return int_format(n, 10, False)


def _parse(text, base):
    """PyLong_FromUnicodeObject and _PyLong_FromBytes."""
    r = int_from_text(text, base)
    if r is MISSING:
        error(ValueError, str_join('', (
            'invalid literal for int() with base ', _decimal(base), ': ', text_repr(text, 200))))
    return r


def to_int(x):
    """PyNumber_Long."""
    if type(x) is int:
        return x
    if type(x) is float:
        return int_from_double(x)       # float.__int__'s own answer
    f = type_lookup(type(x), '__int__')
    if f is not MISSING:
        r = call_special(f, x)
        if type(r) is int:
            return r
        if not kind(r, 'int'):
            error(TypeError, str_join('', ('__int__ returned non-int (type ', _name(r), ')')))
        __import__('warnings').warn(str_join('', (
            '__int__ returned non-int (type ', _name(r), ').  The ability to return an instance of '
            'a strict subclass of int is deprecated, and may be removed in a future version of '
            'Python.')), DeprecationWarning, 1)
        return _exact(r)
    if type_lookup(type(x), '__index__') is not MISSING:
        return index(x)
    if kind(x, 'str') or b_check(x):
        return _parse(x if kind(x, 'str') else b_bytes(x), 10)
    error(TypeError, str_join('', (
        "int() argument must be a string, a bytes-like object or a real number, not '",
        _name(x), "'")))


def _with_base(x, base):
    """long_new_impl with an explicit base."""
    if x is MISSING:
        error(TypeError, 'int() missing string argument')
    base = index(base)
    if (base != 0 and base < 2) or base > 36:
        error(ValueError, 'int() base must be >= 2 and <= 36, or 0')
    if kind(x, 'str') or b_check(x):
        return _parse(x if kind(x, 'str') else b_bytes(x), base)
    error(TypeError, "int() can't convert non-string with explicit base")


def _bytes_of(x):
    """PyObject_Bytes: __bytes__, a bytes-like object, or an iterable of ints."""
    if type(x) is not str and kind(x, 'bytes'):
        return b_bytes(x)
    f = type_lookup(type(x), '__bytes__')
    if f is not MISSING:
        r = call_special(f, x)
        if not kind(r, 'bytes'):
            error(TypeError, str_join('', ('__bytes__ returned non-bytes (type ', _name(r), ')')))
        return b_bytes(r)
    if b_check(x):
        return b_bytes(x)
    if kind(x, 'str') or (type_lookup(type(x), '__iter__') is MISSING
                          and type_lookup(type(x), '__getitem__') is MISSING):
        error(TypeError, str_join('', ("cannot convert '", _name(x), "' object to bytes")))
    return b_from_ints([index(v) for v in x])


def _byteorder(byteorder, fname):
    if not kind(byteorder, 'str'):
        error(TypeError, str_join('', (
            fname, "() argument 'byteorder' must be str, not ", _name(byteorder))))
    if byteorder == 'little':
        return True
    if byteorder == 'big':
        return False
    error(ValueError, "byteorder must be either 'little' or 'big'")


def _int_new(cls, args, kwargs):
    """long_new: int(), int(x), int(x, base) and the keyword form."""
    if cls is not int:
        _boot.check_new(cls, int)
    n = size(args)
    x = MISSING
    base = MISSING
    if kwargs:
        for key, value in ns_items(kwargs):
            if key != 'base':
                error(TypeError, str_join('', ('int() got an unexpected keyword argument \'', key, "'")))
            base = value
        if n > 1:
            error(TypeError, str_join('', ('int() takes at most 2 arguments (', _decimal(n + 1), ' given)')))
    elif n > 2:
        error(TypeError, str_join('', ('int expected at most 2 arguments, got ', _decimal(n))))
    i = 0
    for arg in args:
        if i == 0:
            x = arg
        else:
            base = arg
        i += 1
    if base is MISSING:
        value = 0 if x is MISSING else to_int(x)
    else:
        value = _with_base(x, base)
    if cls is int:
        return value
    return int_subtype(cls, value)


def _int_new1(cls, x):
    """int(x) (kernel_twin): without the argument parsing."""
    if cls is int:
        return to_int(x)
    return _int_new(cls, (x,), {})


@kernel_type(static_type('int'))
class int:
    """int([x]) -> integer
int(x, base=10) -> integer

Convert a number or string to an integer, or return 0 if no arguments
are given.  If x is a number, return x.__int__().  For floating-point
numbers, this truncates towards zero.

If x is not a number or if base is given, then x must be a string,
bytes, or bytearray instance representing an integer literal in the
given base.  The literal can be preceded by '+' or '-' and be surrounded
by whitespace.  The base defaults to 10.  Valid bases are 0 and 2-36.
Base 0 means to interpret the base from the string as an integer
iteral.
>>> int('0b100', base=0)
4"""

    def __new__(cls, /, *args, **kwargs):
        return _int_new(cls, args, kwargs)

    kernel_twin(__new__, _int_new1)

    def __repr__(self):
        return int_format(self, 10, False)

    def __hash__(self):
        return int_hash(self)

    def __lt__(self, other):
        if not kind(other, 'int'):
            return NotImplemented
        return int_cmp(self, other) < 0

    def __le__(self, other):
        if not kind(other, 'int'):
            return NotImplemented
        return int_cmp(self, other) <= 0

    def __eq__(self, other):
        if not kind(other, 'int'):
            return NotImplemented
        return int_cmp(self, other) == 0

    def __ne__(self, other):
        if not kind(other, 'int'):
            return NotImplemented
        return int_cmp(self, other) != 0

    def __gt__(self, other):
        if not kind(other, 'int'):
            return NotImplemented
        return int_cmp(self, other) > 0

    def __ge__(self, other):
        if not kind(other, 'int'):
            return NotImplemented
        return int_cmp(self, other) >= 0

    def __add__(self, other):
        if not kind(other, 'int'):
            return NotImplemented
        return int_add(self, other)

    def __radd__(self, other):
        if not kind(other, 'int'):
            return NotImplemented
        return int_add(other, self)

    def __sub__(self, other):
        if not kind(other, 'int'):
            return NotImplemented
        return int_sub(self, other)

    def __rsub__(self, other):
        if not kind(other, 'int'):
            return NotImplemented
        return int_sub(other, self)

    def __mul__(self, other):
        if not kind(other, 'int'):
            return NotImplemented
        return int_mul(self, other)

    def __rmul__(self, other):
        if not kind(other, 'int'):
            return NotImplemented
        return int_mul(other, self)

    def __mod__(self, other):
        if not kind(other, 'int'):
            return NotImplemented
        return int_mod(self, other)

    def __rmod__(self, other):
        if not kind(other, 'int'):
            return NotImplemented
        return int_mod(other, self)

    def __divmod__(self, other):
        if not kind(other, 'int'):
            return NotImplemented
        return int_divmod(self, other)

    def __rdivmod__(self, other):
        if not kind(other, 'int'):
            return NotImplemented
        return int_divmod(other, self)

    def __pow__(self, other, mod=None):
        if not kind(other, 'int') or not (mod is None or kind(mod, 'int')):
            return NotImplemented
        return int_pow(self, other, mod)

    def __rpow__(self, other, mod=None):
        if not kind(other, 'int') or not (mod is None or kind(mod, 'int')):
            return NotImplemented
        return int_pow(other, self, mod)

    def __neg__(self):
        return int_neg(self)

    def __pos__(self):
        return _exact(self)

    def __abs__(self):
        return int_abs(self)

    def __bool__(self):
        return not int_is_zero(self)

    def __invert__(self):
        return int_invert(self)

    def __lshift__(self, other):
        if not kind(other, 'int'):
            return NotImplemented
        return int_shift(self, other, True)

    def __rlshift__(self, other):
        if not kind(other, 'int'):
            return NotImplemented
        return int_shift(other, self, True)

    def __rshift__(self, other):
        if not kind(other, 'int'):
            return NotImplemented
        return int_shift(self, other, False)

    def __rrshift__(self, other):
        if not kind(other, 'int'):
            return NotImplemented
        return int_shift(other, self, False)

    def __and__(self, other):
        if not kind(other, 'int'):
            return NotImplemented
        return int_bitwise(self, other, 'and')

    def __rand__(self, other):
        if not kind(other, 'int'):
            return NotImplemented
        return int_bitwise(other, self, 'and')

    def __xor__(self, other):
        if not kind(other, 'int'):
            return NotImplemented
        return int_bitwise(self, other, 'xor')

    def __rxor__(self, other):
        if not kind(other, 'int'):
            return NotImplemented
        return int_bitwise(other, self, 'xor')

    def __or__(self, other):
        if not kind(other, 'int'):
            return NotImplemented
        return int_bitwise(self, other, 'or')

    def __ror__(self, other):
        if not kind(other, 'int'):
            return NotImplemented
        return int_bitwise(other, self, 'or')

    def __int__(self):
        return _exact(self)

    def __float__(self):
        return int_as_double(self)

    def __floordiv__(self, other):
        if not kind(other, 'int'):
            return NotImplemented
        return int_floordiv(self, other)

    def __rfloordiv__(self, other):
        if not kind(other, 'int'):
            return NotImplemented
        return int_floordiv(other, self)

    def __truediv__(self, other):
        if not kind(other, 'int'):
            return NotImplemented
        return int_truediv(self, other)

    def __rtruediv__(self, other):
        if not kind(other, 'int'):
            return NotImplemented
        return int_truediv(other, self)

    def __index__(self):
        return _exact(self)

    def conjugate(self):
        """Returns self, the complex conjugate of any int."""
        return _exact(self)

    def bit_length(self):
        """Number of bits necessary to represent self in binary.

>>> bin(37)
'0b100101'
>>> (37).bit_length()
6"""
        return int_bit_length(self)

    def bit_count(self):
        """Number of ones in the binary representation of the absolute value of self.

Also known as the population count.

>>> bin(13)
'0b1101'
>>> (13).bit_count()
3"""
        return int_bit_count(self)

    def to_bytes(self, length=1, byteorder='big', *, signed=False):
        """Return an array of bytes representing an integer.

  length
    Length of bytes object to use.  An OverflowError is raised if the
    integer is not representable with the given number of bytes.  Default
    is length 1.
  byteorder
    The byte order used to represent the integer.  If byteorder is 'big',
    the most significant byte is at the beginning of the byte array.  If
    byteorder is 'little', the most significant byte is at the end of the
    byte array.  To request the native byte order of the host system, use
    sys.byteorder as the byte order value.  Default is to use 'big'.
  signed
    Determines whether two's complement is used to represent the integer.
    If signed is False and a negative integer is given, an OverflowError
    is raised."""
        length = operators.ssize_arg(length)
        little = _byteorder(byteorder, 'to_bytes')
        if length < 0:
            error(ValueError, 'length argument must be non-negative')
        return int_to_bytes(self, length, little, truth(signed))

    @classmethod_
    def from_bytes(cls, bytes, byteorder='big', *, signed=False):
        """Return the integer represented by the given array of bytes.

  bytes
    Holds the array of bytes to convert.  The argument must either
    support the buffer protocol or be an iterable object producing bytes.
    Bytes and bytearray are examples of built-in objects that support the
    buffer protocol.
  byteorder
    The byte order used to represent the integer.  If byteorder is 'big',
    the most significant byte is at the beginning of the byte array.  If
    byteorder is 'little', the most significant byte is at the end of the
    byte array.  To request the native byte order of the host system, use
    sys.byteorder as the byte order value.  Default is to use 'big'.
  signed
    Indicates whether two's complement is used to represent the integer."""
        little = _byteorder(byteorder, 'from_bytes')
        value = int_from_bytes(_bytes_of(bytes), little, truth(signed))
        if cls is int:
            return value
        return cls(value)

    def as_integer_ratio(self):
        """Return a pair of integers, whose ratio is equal to the original int.

The ratio is in lowest terms and has a positive denominator.

>>> (10).as_integer_ratio()
(10, 1)
>>> (-10).as_integer_ratio()
(-10, 1)
>>> (0).as_integer_ratio()
(0, 1)"""
        return (_exact(self), 1)

    def __trunc__(self):
        """Truncating an Integral returns itself."""
        return _exact(self)

    def __floor__(self):
        """Flooring an Integral returns itself."""
        return _exact(self)

    def __ceil__(self):
        """Ceiling of an Integral returns itself."""
        return _exact(self)

    def __round__(self, ndigits=MISSING, /):
        """Rounding an Integral returns itself.

Rounding with an ndigits argument also returns an integer."""
        if ndigits is MISSING:
            return _exact(self)
        ndigits = index(ndigits)
        if ndigits >= 0:
            return _exact(self)
        # self rounded half to even to a multiple of 10 ** -ndigits
        unit = int_pow(10, -ndigits, None)
        q, r = int_divmod(self, unit)
        c = int_cmp(int_mul(r, 2), unit)
        if c > 0 or (c == 0 and int_mod(q, 2) != 0):
            q = int_add(q, 1)
        return int_mul(q, unit)

    def __getnewargs__(self):
        return (_exact(self),)

    def __format__(self, format_spec, /):
        """Convert to a string according to format_spec."""
        if not kind(format_spec, 'str'):
            error(TypeError, str_join('', ('__format__() argument must be str, not ', _name(format_spec))))
        return _format.format_int(self, format_spec)

    def __sizeof__(self):
        """Returns size in memory, in bytes."""
        digits = (int_bit_length(self) + 29) // 30
        return 40 + 4 * (digits if digits > 0 else 1)

    def is_integer(self):
        """Returns True. Exists for duck type compatibility with float.is_integer."""
        return True

    real = getset(_exact)
    imag = getset(lambda self: 0)
    numerator = getset(_exact)
    denominator = getset(lambda self: 1)


def _logic(a, b, op):
    """bool_and, bool_or and bool_xor: a bool for two bools, else the int result."""
    if not kind(b, 'int') or not kind(a, 'int'):
        return NotImplemented
    r = int_bitwise(a, b, op)
    if kind(a, 'bool') and kind(b, 'bool'):
        return r != 0
    return r


@kernel_type(static_type('bool'))
class bool(int):
    """Returns True when the argument is true, False otherwise.
The builtins True and False are the only two instances of the class bool.
The class bool is a subclass of the class int, and cannot be subclassed."""

    def __new__(cls, /, *args, **kwargs):
        if cls is not bool:
            _boot.check_new(cls, bool)
        if kwargs:
            error(TypeError, 'bool() takes no keyword arguments')
        n = size(args)
        if n > 1:
            error(TypeError, str_join('', ('bool expected at most 1 argument, got ', _decimal(n))))
        for arg in args:
            return truth(arg)
        return False

    def __repr__(self):
        return 'True' if self is True else 'False'

    def __invert__(self):
        __import__('warnings').warn(
            "Bitwise inversion '~' on bool is deprecated and will be removed in Python 3.16. "
            "This returns the bitwise inversion of the underlying int object and is usually not "
            "what you expect from negating a bool. Use the 'not' operator for boolean negation or "
            "~int(x) if you really want the bitwise inversion of the underlying int.",
            DeprecationWarning, 1)
        return int_invert(self)

    def __and__(self, other):
        return _logic(self, other, 'and')

    def __rand__(self, other):
        return _logic(other, self, 'and')

    def __or__(self, other):
        return _logic(self, other, 'or')

    def __ror__(self, other):
        return _logic(other, self, 'or')

    def __xor__(self, other):
        return _logic(self, other, 'xor')

    def __rxor__(self, other):
        return _logic(other, self, 'xor')
