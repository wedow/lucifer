"""Argument parsing for kernel functions that take keywords, with the
refusals of the Argument Clinic parser (Python/getargs.c
_PyArg_UnpackKeywords). Functions of positional-only parameters need none:
the interpreter words their arity errors as a C builtin's."""

from _lucifer import MISSING, error, str_join, size, ns_get, ns_items
from _lucifer import _boot, operators

__all__ = ()

decimal = operators.decimal


def _plural(n):
    return '' if n == 1 else 's'


def unpack(fname, args, kwargs, names, posonly, minpos):
    """The values of the parameters `names` (the first `posonly` of them
    positional-only, the first `minpos` required) from args and kwargs, in
    order, MISSING where absent."""
    nargs = size(args)
    nkw = size(kwargs)
    maxargs = size(names)
    if nargs + nkw > maxargs:
        error(TypeError, str_join('', (
            fname, '() takes at most ', decimal(maxargs), ' ', 'keyword ' if nargs == 0 else '',
            'argument', _plural(maxargs), ' (', decimal(nargs + nkw), ' given)')))
    minposonly = posonly if posonly < minpos else minpos
    if nargs < minposonly:
        error(TypeError, str_join('', (
            fname, '() takes ', 'at least' if minposonly < maxargs else 'exactly', ' ',
            decimal(minposonly), ' positional argument', _plural(minposonly), ' (',
            decimal(nargs), ' given)')))
    values = [*args]
    i = nargs if nargs > posonly else posonly
    while i < maxargs:
        v = ns_get(kwargs, names[i], MISSING) if nkw else MISSING
        if v is not MISSING:
            nkw -= 1
        elif i < minpos:
            error(TypeError, str_join('', (
                fname, "() missing required argument '", names[i], "' (pos ", decimal(i + 1), ')')))
        if i >= size(values):
            values = [*values, v]
        i += 1
    while size(values) < maxargs:
        values = [*values, MISSING]
    if nkw:
        for i in range(posonly, nargs):
            if ns_get(kwargs, names[i], MISSING) is not MISSING:
                error(TypeError, str_join('', (
                    'argument for ', fname, "() given by name ('", names[i], "') and position (",
                    decimal(i + 1), ')')))
        for key, _ in ns_items(kwargs):
            if key not in names[posonly:]:
                error(TypeError, str_join('', (
                    fname, "() got an unexpected keyword argument '", key, "'")))
    return values

