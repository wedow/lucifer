"""list (Objects/listobject.c) over the PyListObject layout, and its
iterators. Every mutation is one primitive of runtime/prim/storage.c, which
takes the list's critical section; this file is the protocol around them:
indices, slices, argument checks, comparison and sorting."""

from _lucifer import (
    MISSING, error, str_join, kind, size, ns_items, type_lookup, type_tp_name, static_type,
 seq_get, seq_slice, seq_concat, seq_repeat, tuple_from, list_alloc, list_append,
    list_set, list_splice, list_set_stride, list_del_stride, list_reverse, list_pop,
    list_sort_begin, list_sort_end, iter_new, iter_state, iter_set, seqiter_next, kernel_set,
)
from _lucifer import _boot, operators, slice as _slice, tuple as _tuple, iter as _iter, _listsort

kernel_type = _boot.kernel_type
index = operators.index
as_ssize = operators.as_ssize
decimal = operators.decimal
truth = operators.truth
slice_bounds = _slice.slice_bounds
getitem = _tuple.getitem
compare = _tuple.compare
eq_bool = _tuple.eq_bool
find = _tuple.find
index_args = _tuple.index_args
repeat_count = _tuple.repeat_count
seq_repr = _tuple.seq_repr

__all__ = ('sorted',)

_SSIZE_MAX = 9223372036854775807


def _name(x):
    return type_tp_name(type(x))


def ssize_arg(x):
    """A clinic Py_ssize_t argument: index(x), which must fit."""
    n = index(x)
    if n > _SSIZE_MAX or n < -_SSIZE_MAX - 1:
        error(OverflowError, 'Python int too large to convert to C ssize_t')
    return n


def sequence_fast(value, message):
    """PySequence_Fast: a list or tuple as it is, else the items of the iterable."""
    if kind(value, 'list') or kind(value, 'tuple'):
        return value
    if type_lookup(type(value), '__iter__') is MISSING and \
            (type_lookup(type(value), '__getitem__') is MISSING or kind(value, 'dict')):
        error(TypeError, message)
    return list_of(value)


def list_of(iterable):
    """PySequence_List: the items of any iterable, in a new list."""
    if kind(iterable, 'list') or kind(iterable, 'tuple'):
        return seq_slice(iterable, 0, 1, size(iterable), False)
    items = []
    _iter.extend(items, iterable)
    return items


def sort_into(lst, key, reverse):
    """list.sort over the primitives: the items leave the list while they sort."""
    items = list_sort_begin(lst)
    done = False
    try:
        _listsort.sort(items, key, reverse)
        done = True
    finally:
        modified = list_sort_end(lst, items)
    if done and modified:
        error(ValueError, 'list modified during sort')


def sort_options(args, kwargs):
    """The keyword arguments key and reverse of sort and sorted."""
    if args:
        error(TypeError, 'sort() takes no positional arguments')
    key = None
    reverse = False
    for name, value in ns_items(kwargs):
        if name == 'key':
            key = value
        elif name == 'reverse':
            reverse = True if truth(value) else False
        else:
            error(TypeError, str_join('', ("sort() got an unexpected keyword argument '", name, "'")))
    return key, reverse


def sorted(*args, **kwargs):
    """Return a new list containing all items from the iterable in ascending order.

A custom key function can be supplied to customize the sort order, and the
reverse flag can be set to request the result in descending order."""
    n = size(args)
    if n != 1:
        error(TypeError, str_join('', ('sorted expected 1 argument, got ', decimal(n))))
    result = list_of(args[0])
    key, reverse = sort_options((), kwargs)
    sort_into(result, key, reverse)
    return result


def _append(self, object, /):
    """Append object to the end of the list."""
    list_append(self, object)


# Registered for the call instruction, which compares it: named as the method.
_append.__qualname__ = 'list.append'


@kernel_type(static_type('list'))
class list:
    """Built-in mutable sequence.

If no argument is given, the constructor creates a new empty list.
The argument must be an iterable if specified."""

    def __new__(cls, /, *args, **kwargs):
        if cls is not list:
            operators.check_new(cls, list)
        return list_alloc(cls)

    def __init__(self, /, *args, **kwargs):
        if kwargs and (type(self) is list or type_lookup(type(self), '__new__') is type_lookup(list, '__new__')):
            error(TypeError, 'list() takes no keyword arguments')
        n = size(args)
        if n > 1:
            error(TypeError, str_join('', ('list expected at most 1 argument, got ', decimal(n))))
        if size(self):
            list_splice(self, 0, size(self), ())
        if n == 1:
            list.extend(self, args[0])

    def __len__(self):
        return size(self)

    def __getitem__(self, key):
        return getitem(self, key, 'list')

    def __setitem__(self, key, value):
        if kind(key, 'int') or type_lookup(type(key), '__index__') is not MISSING:
            i = as_ssize(key, IndexError)
            if i < 0:
                i += size(self)
            list_set(self, i, value)
            return
        if not kind(key, 'slice'):
            error(TypeError, str_join('', (
                'list indices must be integers or slices, not ', _name(key))))
        if value is self:
            value = seq_slice(self, 0, 1, size(self), False)
        seq = sequence_fast(value, 'must assign iterable to extended slice')
        start, step, count = slice_bounds(key, size(self))
        if step == 1:
            list_splice(self, start, start + count, seq)
            return
        if size(seq) != count:
            error(ValueError, str_join('', (
                'attempt to assign sequence of size ', decimal(size(seq)),
                ' to extended slice of size ', decimal(count))))
        if count:
            list_set_stride(self, start, step, seq)

    def __delitem__(self, key):
        if kind(key, 'int') or type_lookup(type(key), '__index__') is not MISSING:
            i = as_ssize(key, IndexError)
            if i < 0:
                i += size(self)
            if i < 0 or i >= size(self):
                error(IndexError, 'list assignment index out of range')
            list_splice(self, i, i + 1, ())
            return
        if not kind(key, 'slice'):
            error(TypeError, str_join('', (
                'list indices must be integers or slices, not ', _name(key))))
        start, step, count = slice_bounds(key, size(self))
        if step == 1:
            list_splice(self, start, start + count, ())
        elif count > 0:
            list_del_stride(self, start, step, count)

    def __iter__(self):
        return iter_new(static_type('list_iterator'), self)

    def __reversed__(self):
        """Return a reverse iterator over the list."""
        return iter_new(static_type('list_reverseiterator'), self)

    def __contains__(self, value):
        i = 0
        while i < size(self):
            if eq_bool(seq_get(self, i), value):
                return True
            i += 1
        return False

    def __add__(self, other):
        if not kind(other, 'list'):
            error(TypeError, str_join('', ('can only concatenate list (not "', _name(other), '") to list')))
        return seq_concat(self, other, False)

    def __iadd__(self, other):
        list.extend(self, other)
        return self

    def __mul__(self, other):
        n = repeat_count(other)
        return seq_repeat(self, n, False)

    __rmul__ = __mul__

    def __imul__(self, other):
        n = repeat_count(other)
        if n == 0:
            list_splice(self, 0, size(self), ())
        elif n > 1:
            list_splice(self, 0, size(self), seq_repeat(self, n, False))
        return self

    def __eq__(self, other):
        return compare(self, other, 2, 'list')

    def __ne__(self, other):
        return compare(self, other, 3, 'list')

    def __lt__(self, other):
        return compare(self, other, 0, 'list')

    def __le__(self, other):
        return compare(self, other, 1, 'list')

    def __gt__(self, other):
        return compare(self, other, 4, 'list')

    def __ge__(self, other):
        return compare(self, other, 5, 'list')

    __hash__ = None

    def __repr__(self):
        return seq_repr(self, '[', ']')

    append = _append

    def extend(self, iterable, /):
        """Extend list by appending elements from the iterable."""
        if type(iterable) is list or type(iterable) is tuple:
            list_splice(self, size(self), size(self), iterable)
            return
        _iter.extend(self, iterable)

    def insert(self, index, object, /):
        """Insert object before index."""
        i = ssize_arg(index)
        n = size(self)
        if i < 0:
            i += n
            if i < 0:
                i = 0
        elif i > n:
            i = n
        list_splice(self, i, i, (object,))

    def remove(self, value, /):
        """Remove first occurrence of value.

Raises ValueError if the value is not present."""
        i = 0
        while i < size(self):
            if eq_bool(seq_get(self, i), value):
                list_splice(self, i, i + 1, ())
                return
            i += 1
        error(ValueError, 'list.remove(x): x not in list')

    def pop(self, index=-1, /):
        """Remove and return item at index (default last).

Raises IndexError if list is empty or index is out of range."""
        i = ssize_arg(index)
        if size(self) == 0:
            error(IndexError, 'pop from empty list')
        if i < 0:
            i += size(self)
        v = list_pop(self, i)
        if v is MISSING:
            error(IndexError, 'pop index out of range')
        return v

    def index(self, *args):
        """Return first index of value.

Raises ValueError if the value is not present."""
        value, start, stop = index_args(args, 'list')
        i = find(self, value, start, stop, 'list')
        if i < 0:
            error(ValueError, 'list.index(x): x not in list')
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

    def reverse(self, /):
        """Reverse *IN PLACE*."""
        list_reverse(self)

    def copy(self, /):
        """Return a shallow copy of the list."""
        return seq_slice(self, 0, 1, size(self), False)

    def clear(self, /):
        """Remove all items from list."""
        list_splice(self, 0, size(self), ())

    def sort(self, /, *args, **kwargs):
        """Sort the list in ascending order and return None.

The sort is in-place (i.e. the list itself is modified) and stable (i.e. the
order of two equal elements is maintained).

If a key function is given, apply it once to each list item and sort them,
ascending or descending, according to their function values.

The reverse flag can be set to sort in descending order."""
        key, reverse = sort_options(args, kwargs)
        sort_into(self, key, reverse)


operators.add_sequence(list)


kernel_set('list_append', _append)


@kernel_type(static_type('list_iterator'))
class list_iterator:
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
            return (iter, ([],))
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


@kernel_type(static_type('list_reverseiterator'))
class list_reverseiterator:
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
        n = i + 1
        m = size(seq)
        if n > m:
            n = m
        return n if n > 0 else 0

    def __reduce__(self):
        seq, i = iter_state(self)
        if seq is None:
            return (iter, ([],))
        return (reversed, (seq,), i)

    def __setstate__(self, state):
        seq, i = iter_state(self)
        if seq is not None:
            i = index(state)
            if i < -1:
                i = -1
            elif i > size(seq) - 1:
                i = size(seq) - 1
            iter_set(self, seq, i)


_boot.builtin_sequence(list)
