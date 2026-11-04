"""slice (Objects/sliceobject.c) and the index arithmetic the sequence
types share: slice bounds against a length, and the sequence index
conversions of their error messages."""

from _lucifer import (
    MISSING, error, str_join, kind, size, type_lookup, type_tp_name, static_type,
    slice_new, slice_get, xxh_hash, int_add,
)
from _lucifer import _boot, _format, operators

kernel_type = _boot.kernel_type
getset = _boot.getset
index = operators.index
decimal = operators.decimal
hash_of = operators.hash_of
object_repr = _format.to_repr

__all__ = ()


def slice_index(x):
    """_PyEval_SliceIndex: a bound, which must be an int or have __index__."""
    if kind(x, 'int') or type_lookup(type(x), '__index__') is not MISSING:
        return index(x)
    error(TypeError, 'slice indices must be integers or None or have an __index__ method')


def slice_index_not_none(x):
    """_PyEval_SliceIndexNotNone: the start and stop of list.index and friends."""
    if kind(x, 'int') or type_lookup(type(x), '__index__') is not MISSING:
        return index(x)
    error(TypeError, 'slice indices must be integers or have an __index__ method')


_SSIZE_MAX = 0x7fffffffffffffff


def slice_bounds(slc, length):
    """PySlice_Unpack and PySlice_AdjustIndices: (start, step, count) of slc
    over a sequence of the given length. Ints are unbounded, so a bound
    beyond Py_ssize_t needs no clipping."""
    step = slice_get(slc, 2)
    if step is None:
        step = 1
    else:
        step = slice_index(step)
        if step == 0:
            error(ValueError, 'slice step cannot be zero')
        if step > _SSIZE_MAX:
            step = _SSIZE_MAX
        elif step < -_SSIZE_MAX:
            step = -_SSIZE_MAX
    start = slice_get(slc, 0)
    stop = slice_get(slc, 1)
    if start is None:
        start = length - 1 if step < 0 else 0
    else:
        start = slice_index(start)
        if start < 0:
            start += length
            if start < 0:
                start = -1 if step < 0 else 0
        elif start >= length:
            start = length - 1 if step < 0 else length
    if stop is None:
        stop = -1 if step < 0 else length
    else:
        stop = slice_index(stop)
        if stop < 0:
            stop += length
            if stop < 0:
                stop = -1 if step < 0 else 0
        elif stop >= length:
            stop = length - 1 if step < 0 else length
    if step < 0:
        count = (start - stop - 1) // -step + 1 if stop < start else 0
    else:
        count = (stop - start - 1) // step + 1 if start < stop else 0
    return start, step, count


def _indices(slc, length):
    """_PySlice_GetLongIndices: (start, stop, step) clipped to length."""
    step = slice_get(slc, 2)
    if step is None:
        step = 1
    else:
        step = slice_index(step)
        if step == 0:
            error(ValueError, 'slice step cannot be zero')
    if step < 0:
        lower = -1
        upper = length - 1
    else:
        lower = 0
        upper = length
    start = slice_get(slc, 0)
    if start is None:
        start = upper if step < 0 else lower
    else:
        start = slice_index(start)
        if start < 0:
            start += length
            if start < lower:
                start = lower
        elif start > upper:
            start = upper
    stop = slice_get(slc, 1)
    if stop is None:
        stop = lower if step < 0 else upper
    else:
        stop = slice_index(stop)
        if stop < 0:
            stop += length
            if stop < lower:
                stop = lower
        elif stop > upper:
            stop = upper
    return start, stop, step


def _triple(s):
    return (slice_get(s, 0), slice_get(s, 1), slice_get(s, 2))


@kernel_type(static_type('slice'))
class slice:
    """slice(stop)
slice(start, stop[, step])

Create a slice object.  This is used for extended slicing (e.g. a[0:10:2])."""

    def __new__(cls, /, *args, **kwargs):
        operators.check_new(cls, slice)
        if kwargs:
            error(TypeError, 'slice() takes no keyword arguments')
        n = size(args)
        if n == 0:
            error(TypeError, 'slice expected at least 1 argument, got 0')
        if n > 3:
            error(TypeError, str_join('', ('slice expected at most 3 arguments, got ', decimal(n))))
        if n == 1:
            return slice_new(None, args[0], None)
        return slice_new(args[0], args[1], args[2] if n == 3 else None)

    def __repr__(self):
        return str_join('', ('slice(', object_repr(slice_get(self, 0)), ', ',
                             object_repr(slice_get(self, 1)), ', ',
                             object_repr(slice_get(self, 2)), ')'))

    def __hash__(self):
        return xxh_hash((hash_of(slice_get(self, 0)), hash_of(slice_get(self, 1)),
                         hash_of(slice_get(self, 2))), False)

    def __reduce__(self):
        return (slice, _triple(self))

    def __eq__(self, other):
        if not kind(other, 'slice'):
            return NotImplemented
        return _triple(self) == _triple(other)

    def __ne__(self, other):
        if not kind(other, 'slice'):
            return NotImplemented
        return _triple(self) != _triple(other)

    def __lt__(self, other):
        if not kind(other, 'slice'):
            return NotImplemented
        return _triple(self) < _triple(other)

    def __le__(self, other):
        if not kind(other, 'slice'):
            return NotImplemented
        return _triple(self) <= _triple(other)

    def __gt__(self, other):
        if not kind(other, 'slice'):
            return NotImplemented
        return _triple(self) > _triple(other)

    def __ge__(self, other):
        if not kind(other, 'slice'):
            return NotImplemented
        return _triple(self) >= _triple(other)

    def indices(self, length, /):
        """S.indices(len) -> (start, stop, stride)

Assuming a sequence of length len, calculate the start and stop
indices, and the stride length of the extended slice described by
S. Out of bounds indices are clipped in a manner consistent with the
handling of normal slices."""
        length = index(length)
        if length < 0:
            error(ValueError, 'length should not be negative')
        return _indices(self, length)

    start = getset(lambda self: slice_get(self, 0))
    stop = getset(lambda self: slice_get(self, 1))
    step = getset(lambda self: slice_get(self, 2))


@kernel_type(static_type('ellipsis'))
class ellipsis:
    def __repr__(self):
        return 'Ellipsis'

    def __reduce__(self):
        return 'Ellipsis'
