"""_operator (Modules/_operator.c): the operator functions. operator.py defines
them in Python and takes this module's names over when it exists; here they
are operator's own, for C extensions that import _operator by name."""

import operator as _operator_py
from operator import *
from _lucifer import operators as _kernel_operators

index = _kernel_operators.index     # PyNumber_Index: a TypeError for what has no __index__


def _compare_digest(a, b, /):
    """Return 'a == b'.

This function uses an approach designed to prevent timing analysis, making it appropriate for cryptography."""
    if isinstance(a, str) and isinstance(b, str):
        try:
            a, b = a.encode('ascii'), b.encode('ascii')
        except UnicodeEncodeError:
            raise TypeError('comparing strings with non-ASCII characters is not supported') from None
    elif isinstance(a, str) or isinstance(b, str):
        raise TypeError("a bytes-like object is required, not 'str'")
    else:
        try:
            a, b = bytes(memoryview(a)), bytes(memoryview(b))
        except TypeError:
            raise TypeError('unsupported operand types(s) or combination of types: %r and %r'
                            % (type(a).__name__, type(b).__name__)) from None
    result = len(a) ^ len(b)
    for x, y in zip(a, b if len(b) == len(a) else a):
        result |= x ^ y
    return result == 0
