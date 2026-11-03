"""tuple (Objects/tupleobject.c) over the PyTupleObject layout, and its
iterator. Indexing, slicing and concatenation are primitives of
runtime/prim/storage.c; the rest is the protocol around them."""

from _lucifer import (
    MISSING, error, str_join, kind, size, type_lookup, type_tp_name, static_type, kernel_set,
 seq_get, seq_slice, seq_concat, seq_repeat, tuple_from, xxh_hash, repr_enter,
    repr_leave, iter_new, iter_state, iter_set, seqiter_next, list_append,
)
from _lucifer import _boot, operators, slice as _slice, iter as _iter

kernel_type = _boot.kernel_type
classmethod_ = _boot.classmethod_
index = operators.index
as_ssize = operators.as_ssize
decimal = operators.decimal
hash_of = operators.hash_of
object_repr = operators.object_repr
slice_bounds = _slice.slice_bounds
slice_index_not_none = _slice.slice_index_not_none

__all__ = ()

_SSIZE_MAX = 9223372036854775807


def _name(x):
    return type_tp_name(type(x))


def getitem(self, key, what):
    """list_subscript and tuplesubscript: self[key] for a tuple or list
    `what` ('tuple' or 'list') by index or slice; a slice answers an exact
    object of the same kind, a whole exact tuple answers itself."""
    if kind(key, 'int') or type_lookup(type(key), '__index__') is not MISSING:
        i = as_ssize(key, IndexError)
        n = size(self)
        if i < 0:
            i += n
        if i < 0 or i >= n:
            error(IndexError, str_join('', (what, ' index out of range')))
        return seq_get(self, i)
    if kind(key, 'slice'):
        n = size(self)
        start, step, count = slice_bounds(key, n)
        if what == 'tuple' and start == 0 and step == 1 and count == n and type(self) is tuple:
            return self
        return seq_slice(self, start, step, count, what == 'tuple')
    error(TypeError, str_join('', (
        what, ' indices must be integers or slices, not ', _name(key))))


def eq_bool(a, b):
    """PyObject_RichCompareBool(a, b, Py_EQ)."""
    return True if a is b or a == b else False


def find(self, value, start, stop, what):
    """index(): the first position of value in self[start:stop], or -1;
    start and stop are Python ints, normalized as list_index_impl does."""
    n = size(self)
    if start < 0:
        start += n
        if start < 0:
            start = 0
    if stop < 0:
        stop += n
        if stop < 0:
            stop = 0
    i = start
    while i < stop and i < size(self):
        if eq_bool(seq_get(self, i), value):
            return i
        i += 1
    return -1


def index_args(args, name):
    """index(value, start=0, stop=maxsize, /): (value, start, stop)."""
    n = size(args)
    if n == 0:
        error(TypeError, str_join('', (name, '.index expected at least 1 argument, got 0')))
    if n > 3:
        error(TypeError, str_join('', (name, '.index expected at most 3 arguments, got ', decimal(n))))
    start = slice_index_not_none(args[1]) if n > 1 else 0
    stop = slice_index_not_none(args[2]) if n > 2 else _SSIZE_MAX
    return args[0], start, stop


def compare(self, other, op, what):
    """list_richcompare and tuplerichcompare: op is 0..5 for < <= == != > >=."""
    if not kind(other, what):
        return NotImplemented
    vlen = size(self)
    wlen = size(other)
    if what == 'list' and vlen != wlen and (op == 2 or op == 3):
        return op == 3
    i = 0
    while i < size(self) and i < size(other):
        v = seq_get(self, i)
        w = seq_get(other, i)
        if not eq_bool(v, w):
            break
        i += 1
    if i >= size(self) or i >= size(other):
        vlen = size(self)
        wlen = size(other)
        if op == 0:
            return vlen < wlen
        if op == 1:
            return vlen <= wlen
        if op == 2:
            return vlen == wlen
        if op == 3:
            return vlen != wlen
        if op == 4:
            return vlen > wlen
        return vlen >= wlen
    if op == 2:
        return False
    if op == 3:
        return True
    v = seq_get(self, i)
    w = seq_get(other, i)
    if op == 0:
        return v < w
    if op == 1:
        return v <= w
    if op == 4:
        return v > w
    return v >= w


def repeat_count(other):
    """The count of seq * other, or NotImplemented when other is neither an
    int nor has __index__ (operators.binary_op words the refusal)."""
    if kind(other, 'int') or type_lookup(type(other), '__index__') is not MISSING:
        n = as_ssize(other, OverflowError)
        return n if n > 0 else 0
    return NotImplemented


def seq_repr(self, opener, closer):
    n = size(self)
    if n == 0:
        return str_join('', (opener, closer))
    if repr_enter(self):
        return str_join('', (opener, '...', closer))
    try:
        parts = []
        i = 0
        while i < size(self):
            list_append(parts, object_repr(seq_get(self, i)))
            i += 1
    finally:
        repr_leave(self)
    return str_join('', (opener, str_join(', ', (*parts,)), closer))


@kernel_type(static_type('tuple'))
class tuple:
    """Built-in immutable sequence.

If no argument is given, the constructor returns an empty tuple.
If iterable is specified the tuple is initialized from iterable's items.

If the argument is a tuple, the return value is the same object."""

    def __new__(cls, *args, **kwargs):
        operators.check_new(cls, tuple)
        if kwargs:
            error(TypeError, 'tuple() takes no keyword arguments')
        n = size(args)
        if n > 1:
            error(TypeError, str_join('', ('tuple expected at most 1 argument, got ', decimal(n))))
        items = _iter.tuple_of(args[0]) if n == 1 else ()
        return tuple_from(items, cls)

    def __len__(self):
        return size(self)

    def __getitem__(self, key):
        return getitem(self, key, 'tuple')

    def __iter__(self):
        return iter_new(static_type('tuple_iterator'), self)

    def __contains__(self, value):
        i = 0
        while i < size(self):
            if eq_bool(seq_get(self, i), value):
                return True
            i += 1
        return False

    def __add__(self, other):
        if not kind(other, 'tuple'):
            return NotImplemented
        if size(other) == 0 and type(self) is tuple:
            return self
        if size(self) == 0 and type(other) is tuple:
            return other
        return seq_concat(self, other, True)

    def __mul__(self, other):
        n = repeat_count(other)
        if n is NotImplemented:
            return n
        if n == 1 and type(self) is tuple:
            return self
        return seq_repeat(self, n, True)

    __rmul__ = __mul__

    def __eq__(self, other):
        return compare(self, other, 2, 'tuple')

    def __ne__(self, other):
        return compare(self, other, 3, 'tuple')

    def __lt__(self, other):
        return compare(self, other, 0, 'tuple')

    def __le__(self, other):
        return compare(self, other, 1, 'tuple')

    def __gt__(self, other):
        return compare(self, other, 4, 'tuple')

    def __ge__(self, other):
        return compare(self, other, 5, 'tuple')

    def __hash__(self):
        hashes = []
        i = 0
        while i < size(self):
            list_append(hashes, hash_of(seq_get(self, i)))
            i += 1
        return xxh_hash(hashes, True)

    def __repr__(self):
        if size(self) == 1:
            return str_join('', ('(', object_repr(seq_get(self, 0)), ',)'))
        return seq_repr(self, '(', ')')

    def __getnewargs__(self):
        return (tuple_from(self, tuple),)

    def index(self, *args):
        """Return first index of value.

Raises ValueError if the value is not present."""
        value, start, stop = index_args(args, 'tuple')
        i = find(self, value, start, stop, 'tuple')
        if i < 0:
            error(ValueError, 'tuple.index(x): x not in tuple')
        return i

    def count(self, value, /):
        """Return number of occurrences of value."""
        n = 0
        i = 0
        while i < size(self):
            if eq_bool(seq_get(self, i), value):
                n += 1
            i += 1
        return n


def _concat_error(a, b):
    error(TypeError, str_join('', ('can only concatenate tuple (not "', _name(b), '") to tuple')))


operators.register_sequence(tuple, _concat_error)


@kernel_type(static_type('tuple_iterator'))
class tuple_iterator:
    def __iter__(self):
        return self

    def __next__(self):
        v = seqiter_next(self)
        if v is MISSING:
            raise StopIteration
        return v

    def __length_hint__(self):
        seq, i = iter_state(self)
        if seq is None:
            return 0
        n = size(seq) - i
        return n if n > 0 else 0

    def __reduce__(self):
        seq, i = iter_state(self)
        if seq is None:
            return (iter, ((),))
        return (iter, (seq,), i)

    def __setstate__(self, state):
        seq, i = iter_state(self)
        if seq is not None:
            i = index(state)
            if i < 0:
                i = 0
            elif i > size(seq):
                i = size(seq)
            iter_set(self, seq, i)
