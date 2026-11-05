"""_operator (Modules/_operator.c): the operator functions. operator.py defines
them in Python and takes this module's names over when it exists; here they
are operator.py's own definitions, run in a namespace of their own rather than
imported, so that operator takes this module's builtins whichever of the two
is imported first."""

from _lucifer import frozen_code as _frozen_code, operators as _kernel_operators


def _definitions():
    names = {'__name__': 'operator', '__builtins__': __builtins__}
    exec(_frozen_code('operator'), names)
    return {name: names[name] for name in names['__all__']}


globals().update(_definitions())
del _definitions

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
