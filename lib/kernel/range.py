"""range (Objects/rangeobject.c): immutable arithmetic progressions of
unbounded ints, and the iterators over them. The four ints live in the
range's layout; everything else is integer arithmetic here."""

from _lucifer import (
    MISSING, error, str_join, kind, size, type_lookup, type_tp_name, static_type,
    range_new, range_get, range_iter, range_iter_new, rangeiter_next, rangeiter_len,
    xxh_hash, int_add,
)
from _lucifer import _boot, _format, operators, slice as _slice, iter as _iter

kernel_type = _boot.kernel_type
getset = _boot.getset
index = operators.index
decimal = operators.decimal
hash_of = operators.hash_of
object_repr = _format.to_repr
slice_indices = _slice._indices

__all__ = ()

_SSIZE_MAX = 9223372036854775807


def _name(x):
    return type_tp_name(type(x))


def _length(start, stop, step):
    """compute_range_length."""
    if step > 0:
        lo = start
        hi = stop
    else:
        lo = stop
        hi = start
        step = -step
    if lo >= hi:
        return 0
    return (hi - lo - 1) // step + 1


def _make(start, stop, step):
    return range_new(start, stop, step, _length(start, stop, step))


def _contains_int(r, ob):
    """range_contains_long."""
    start = range_get(r, 0)
    stop = range_get(r, 1)
    step = range_get(r, 2)
    if step > 0:
        inside = start <= ob and ob < stop
    else:
        inside = ob <= start and stop < ob
    return inside and (ob - start) % step == 0


def _is_int(ob):
    return type(ob) is int or type(ob) is bool


def _search(r, ob, what):
    """_PySequence_IterSearch over a range, for the values equal to ob:
    their count, or the position of the first."""
    it = _iter.get_iter(r)
    n = 0
    while True:
        x = _iter.next_item(it)
        if x is MISSING:
            return n if what == 'count' else -1
        if x is ob or x == ob:
            if what == 'index':
                return n
            n += 1
        elif what == 'index':
            n += 1


@kernel_type(static_type('range'))
class range:
    """range(stop) -> range object
range(start, stop[, step]) -> range object

Return an object that produces a sequence of integers from start (inclusive)
to stop (exclusive) by step.  range(i, j) produces i, i+1, i+2, ..., j-1.
start defaults to 0, and stop is omitted!  range(4) produces 0, 1, 2, 3.
These are exactly the valid indices for a list of 4 elements.
When step is given, it specifies the increment (or decrement)."""

    def __new__(cls, /, *args, **kwargs):
        if cls is not range:
            operators.check_new(cls, range)
        if kwargs:
            error(TypeError, 'range() takes no keyword arguments')
        n = size(args)
        if n == 1 and type(args[0]) is int:
            stop = args[0]
            return range_new(0, stop, 1, stop if stop > 0 else 0)
        if n == 0:
            error(TypeError, 'range expected at least 1 argument, got 0')
        if n > 3:
            error(TypeError, str_join('', ('range expected at most 3 arguments, got ', decimal(n))))
        if n == 1:
            return _make(0, index(args[0]), 1)
        start = index(args[0])
        stop = index(args[1])
        step = index(args[2]) if n == 3 else 1
        if step == 0:
            error(ValueError, 'range() arg 3 must not be zero')
        return _make(start, stop, step)

    start = getset(lambda self: range_get(self, 0))
    stop = getset(lambda self: range_get(self, 1))
    step = getset(lambda self: range_get(self, 2))

    def __len__(self):
        n = range_get(self, 3)
        if n > _SSIZE_MAX:
            error(OverflowError, 'Python int too large to convert to C ssize_t')
        return n

    def __bool__(self):
        return range_get(self, 3) != 0

    def __iter__(self):
        return range_iter(self)

    def __reversed__(self):
        """Return a reverse iterator."""
        n = range_get(self, 3)
        step = range_get(self, 2)
        return range_iter_new(range_get(self, 0) + (n - 1) * step, -step, n)

    def __getitem__(self, key):
        if kind(key, 'int') or type_lookup(type(key), '__index__') is not MISSING:
            i = index(key)
            n = range_get(self, 3)
            if i < 0:
                i += n
            if i < 0 or i >= n:
                error(IndexError, 'range object index out of range')
            return range_get(self, 0) + i * range_get(self, 2)
        if kind(key, 'slice'):
            lo, hi, step = slice_indices(key, range_get(self, 3))
            rstart = range_get(self, 0)
            rstep = range_get(self, 2)
            return _make(rstart + lo * rstep, rstart + hi * rstep, step * rstep)
        error(TypeError, str_join('', (
            'range indices must be integers or slices, not ', _name(key))))

    def __contains__(self, ob):
        if _is_int(ob):
            return True if _contains_int(self, ob) else False
        return _search(self, ob, 'index') >= 0

    def __eq__(self, other):
        if type(other) is not range:
            return NotImplemented
        return _equals(self, other)

    def __ne__(self, other):
        if type(other) is not range:
            return NotImplemented
        return not _equals(self, other)

    def __hash__(self):
        n = range_get(self, 3)
        if n == 0:
            items = (n, None, None)
        elif n == 1:
            items = (n, range_get(self, 0), None)
        else:
            items = (n, range_get(self, 0), range_get(self, 2))
        return xxh_hash((hash_of(items[0]), hash_of(items[1]), hash_of(items[2])), True)

    def __repr__(self):
        if range_get(self, 2) == 1:
            return str_join('', ('range(', object_repr(range_get(self, 0)), ', ',
                                 object_repr(range_get(self, 1)), ')'))
        return str_join('', ('range(', object_repr(range_get(self, 0)), ', ',
                             object_repr(range_get(self, 1)), ', ',
                             object_repr(range_get(self, 2)), ')'))

    def __reduce__(self):
        return (range, (range_get(self, 0), range_get(self, 1), range_get(self, 2)))

    def count(self, ob, /):
        """rangeobject.count(value) -> integer -- return number of occurrences of value"""
        if _is_int(ob):
            return 1 if _contains_int(self, ob) else 0
        return _search(self, ob, 'count')

    def index(self, ob, /):
        """rangeobject.index(value) -> integer -- return index of value.
Raise ValueError if the value is not present."""
        if not _is_int(ob):
            i = _search(self, ob, 'index')
            if i < 0:
                error(ValueError, 'sequence.index(x): x not in sequence')
            return i
        if _contains_int(self, ob):
            return (ob - range_get(self, 0)) // range_get(self, 2)
        error(ValueError, 'range.index(x): x not in range')


def _equals(r0, r1):
    """range_equals."""
    if r0 is r1:
        return True
    n = range_get(r0, 3)
    if n != range_get(r1, 3):
        return False
    if n == 0:
        return True
    if range_get(r0, 0) != range_get(r1, 0):
        return False
    if n == 1:
        return True
    return range_get(r0, 2) == range_get(r1, 2)


@kernel_type(static_type('range_iterator'))
class range_iterator:
    def __iter__(self):
        return self

    def __next__(self):
        v = rangeiter_next(self)
        if v is MISSING:
            raise StopIteration
        return v

    def __length_hint__(self):
        return rangeiter_len(self)


@kernel_type(static_type('longrange_iterator'))
class longrange_iterator:
    def __iter__(self):
        return self

    def __next__(self):
        v = rangeiter_next(self)
        if v is MISSING:
            raise StopIteration
        return v

    def __length_hint__(self):
        return rangeiter_len(self)


# The iteration step without StopIteration (iter.py).
_iter.next_missing_set(range_iterator, rangeiter_next)
_iter.next_missing_set(longrange_iterator, rangeiter_next)
