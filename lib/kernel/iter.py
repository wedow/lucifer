"""The iteration protocol (Objects/abstract.c, iterobject.c): iter() over
__iter__ or the sequence protocol, the step FOR_ITER takes, containment by
iteration, unpacking and the list extension of `*`. The interpreter pushes
these functions' frames, so iterating never recurses in C (DESIGN 5.2); the
exact tuple, list and range loops never come here."""

from _lucifer import (
    MISSING, error, str_join, kind, size, type_lookup, type_tp_name, static_type, kernel_set,
    iter_new, iter_state, iter_set, list_append, list_splice, tuple_from, is_callable,
    alloc, field_get, field_set, seqiter_next, dictiter_next,
)
from _lucifer import _boot, operators

kernel_type = _boot.kernel_type
call_special = _boot.call_special
index = operators.index
decimal = operators.decimal

__all__ = ('iter', 'next')


def _name(x):
    return type_tp_name(type(x))


def seqiter_reduce(self, empty, builtin_iter):
    """__reduce__ of an iterator over a sequence: iter(seq) and the index; an
    exhausted one rebuilds as an iterator over `empty`. The caller passes the
    builtin iter, which this module's own iter shadows."""
    seq, i = iter_state(self)
    if seq is None:
        return (builtin_iter, (empty,))
    return (builtin_iter, (seq,), i)


def seqiter_setstate(self, state):
    """__setstate__ of an iterator over a sequence: the index, clipped to the sequence."""
    seq, i = iter_state(self)
    if seq is not None:
        i = index(state)
        if i < 0:
            i = 0
        elif i > operators.length(seq):
            i = operators.length(seq)
        iter_set(self, seq, i)


def _has_sequence_protocol(obj):
    """PySequence_Check: __getitem__ on a type that is not a mapping."""
    return type_lookup(type(obj), '__getitem__') is not MISSING and not kind(obj, 'dict')


def get_iter(obj):
    """PyObject_GetIter."""
    f = type_lookup(type(obj), '__iter__')
    if f is not MISSING and f is not None:
        it = call_special(f, obj)
        if type_lookup(type(it), '__next__') is MISSING:
            error(TypeError, str_join('', ("iter() returned non-iterator of type '", _name(it), "'")))
        return it
    if f is None or not _has_sequence_protocol(obj):
        error(TypeError, str_join('', ("'", _name(obj), "' object is not iterable")))
    return iter_new(static_type('iterator'), obj)


_SEQ_ITERATORS = (static_type('tuple_iterator'), static_type('list_iterator'))
_DICT_ITERATORS = (static_type('dict_keyiterator'), static_type('dict_valueiterator'),
                   static_type('dict_itemiterator'))
_seq0, _seq1 = _SEQ_ITERATORS
_dict0, _dict1, _dict2 = _DICT_ITERATORS


# Exact iterator type -> a function giving its next item, or MISSING at the end,
# without raising StopIteration (iterators.py registers zip and enumerate).
NEXT_MISSING = {}


def next_item(it):
    """The step of FOR_ITER and next(): the next item, or MISSING when the
    iterator is exhausted."""
    t = type(it)
    if t is _seq0 or t is _seq1:
        return seqiter_next(it)
    if t is _dict0 or t is _dict1 or t is _dict2:
        return dictiter_next(it)
    f = NEXT_MISSING.get(t)
    if f is not None:
        return f(it)
    f = type_lookup(type(it), '__next__')
    if f is MISSING:
        error(TypeError, str_join('', ("'", _name(it), "' object is not an iterator")))
    try:
        return call_special(f, it)
    except StopIteration:
        return MISSING


def contains(container, item, invert):
    """CONTAINS_OP: item in container (or not in, when invert): __contains__,
    else the first iterated element that is item or equals it."""
    f = type_lookup(type(container), '__contains__')
    if f is not MISSING:
        found = True if call_special(f, container, item) else False
    else:
        refused = False
        try:
            it = get_iter(container)
        except TypeError:
            refused = True
        if refused:
            error(TypeError, str_join('', (
                "argument of type '", _name(container), "' is not a container or iterable")))
        found = False
        while True:
            x = next_item(it)
            if x is MISSING:
                break
            if x is item or x == item:
                found = True
                break
    return not found if invert else found


def _unpack_iter(obj):
    if type_lookup(type(obj), '__iter__') is MISSING and not _has_sequence_protocol(obj):
        error(TypeError, str_join('', ('cannot unpack non-iterable ', _name(obj), ' object')))
    return get_iter(obj)


def unpack(obj, n):
    """The n items of an iterable (UNPACK_SEQUENCE), as a tuple."""
    it = _unpack_iter(obj)
    items = []
    while size(items) < n:
        x = next_item(it)
        if x is MISSING:
            error(ValueError, str_join('', (
                'not enough values to unpack (expected ', decimal(n), ', got ',
                decimal(size(items)), ')')))
        list_append(items, x)
    if next_item(it) is not MISSING:
        if type(obj) is list or type(obj) is tuple:
            error(ValueError, str_join('', (
                'too many values to unpack (expected ', decimal(n), ', got ', decimal(size(obj)), ')')))
        if type(obj) is dict:
            error(ValueError, str_join('', (
                'too many values to unpack (expected ', decimal(n), ', got ', decimal(size(obj)), ')')))
        error(ValueError, str_join('', ('too many values to unpack (expected ', decimal(n), ')')))
    return tuple_from(items, tuple)


def unpack_ex(obj, before, after):
    """UNPACK_EX: the `before` first items, the rest as a list, the `after`
    last items, as a tuple."""
    it = _unpack_iter(obj)
    items = []
    while size(items) < before:
        x = next_item(it)
        if x is MISSING:
            error(ValueError, str_join('', (
                'not enough values to unpack (expected at least ', decimal(before + after),
                ', got ', decimal(size(items)), ')')))
        list_append(items, x)
    rest = []
    while True:
        x = next_item(it)
        if x is MISSING:
            break
        list_append(rest, x)
    if size(rest) < after:
        error(ValueError, str_join('', (
            'not enough values to unpack (expected at least ', decimal(before + after),
            ', got ', decimal(before + size(rest)), ')')))
    tail = size(rest) - after
    last = rest[tail:]
    del rest[tail:]
    list_append(items, rest)
    list_splice(items, size(items), size(items), last)
    return tuple_from(items, tuple)


def extend(lst, iterable):
    """list.extend over any iterable: PyObject_LengthHint asks for __len__
    first, and its error, other than a TypeError, ends the extension."""
    it = get_iter(iterable)
    if type_lookup(type(iterable), '__len__') is not MISSING:
        try:
            operators.length(iterable)
        except TypeError:
            pass
    while True:
        x = next_item(it)
        if x is MISSING:
            return
        list_append(lst, x)


def list_extend(lst, iterable):
    """LIST_EXTEND: the items of iterable onto lst."""
    if type_lookup(type(iterable), '__iter__') is MISSING and not _has_sequence_protocol(iterable):
        error(TypeError, str_join('', ('Value after * must be an iterable, not ', _name(iterable))))
    extend(lst, iterable)


def tuple_of(iterable):
    """PySequence_Tuple."""
    if type(iterable) is tuple:
        return iterable
    items = []
    it = get_iter(iterable)
    while True:
        x = next_item(it)
        if x is MISSING:
            return tuple_from(items, tuple)
        list_append(items, x)


def iter(obj, sentinel=MISSING, /):
    """Get an iterator from an object.

In the first form, the argument must supply its own iterator, or be a sequence.
In the second form, the callable is called until it returns the sentinel."""
    if sentinel is MISSING:
        return get_iter(obj)
    if not is_callable(obj):
        error(TypeError, 'iter(v, w): v must be callable')
    it = alloc(callable_iterator, callable_iterator)
    field_set(it, 0, obj)
    field_set(it, 1, sentinel)
    return it


def next(it, default=MISSING, /):
    """Return the next item from the iterator.

If default is given and the iterator is exhausted,
it is returned instead of raising StopIteration."""
    if type_lookup(type(it), '__next__') is MISSING:
        error(TypeError, str_join('', ("'", _name(it), "' object is not an iterator")))
    if default is MISSING:
        return call_special(type_lookup(type(it), '__next__'), it)
    x = next_item(it)
    return default if x is MISSING else x


@kernel_type(static_type('iterator'))
class iterator:
    def __iter__(self):
        return self

    def __next__(self):
        seq, i = iter_state(self)
        if seq is None:
            raise StopIteration
        try:
            v = operators.getitem(seq, i)
        except (IndexError, StopIteration):
            iter_set(self, None, i)
            raise StopIteration
        iter_set(self, seq, i + 1)
        return v

    def __length_hint__(self):
        seq, i = iter_state(self)
        if seq is None:
            return 0
        if type_lookup(type(seq), '__len__') is MISSING:
            return NotImplemented
        n = operators.length(seq) - i
        return n if n > 0 else 0


@kernel_type(static_type('callable_iterator'))
class callable_iterator:
    def __iter__(self):
        return self

    def __next__(self):
        func = field_get(self, 0)
        if func is None:
            raise StopIteration
        try:
            value = func()
        except StopIteration:
            value = MISSING
        else:
            sentinel = field_get(self, 1)
            if value is sentinel or value == sentinel:
                value = MISSING
        if value is MISSING:
            field_set(self, 0, None)
            field_set(self, 1, None)
            raise StopIteration
        return value


kernel_set('get_iter', get_iter)
kernel_set('next_item', next_item)
kernel_set('contains', contains)
kernel_set('unpack', unpack)
kernel_set('unpack_ex', unpack_ex)
kernel_set('list_extend', list_extend)
kernel_set('tuple_of', tuple_of)
