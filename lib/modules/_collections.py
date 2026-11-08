"""_collections (Modules/_collectionsmodule.c): deque over a ring buffer,
defaultdict, _count_elements and _tuplegetter. OrderedDict stays the
pure-Python fallback of collections."""

import sys
from _lucifer import (
    MISSING, repr_enter, repr_leave, kind, size, seq_get, kernel_set, ring_new, ring_push, ring_extend, ring_pop,
    ring_get, ring_set, ring_del, ring_insert, ring_rotate, ring_reverse, ring_clear, ring_items, ring_len, ring_state,
)
from _types import GenericAlias

__all__ = ('deque', 'defaultdict', '_deque_iterator', '_deque_reverse_iterator',
           '_count_elements', '_tuplegetter')


def _index(x):
    """PyNumber_AsSsize_t: an int or an object with __index__."""
    if isinstance(x, int):
        return x
    try:
        method = type(x).__index__
    except AttributeError:
        raise TypeError("'%s' object cannot be interpreted as an integer" % type(x).__name__) from None
    return method(x)


def _slice_index(x):
    if isinstance(x, int) or hasattr(type(x), '__index__'):
        return _index(x)
    raise TypeError('slice indices must be integers or have an __index__ method')


class deque:
    """A list-like sequence optimized for data accesses near its endpoints."""

    __slots__ = ('_ring', '_maxlen', '__weakref__')
    __hash__ = None

    def __new__(cls, *args, **kwargs):
        self = object.__new__(cls)
        self._ring = ring_new()
        self._maxlen = None
        return self

    def __init__(self, iterable=(), maxlen=None):
        if maxlen is not None:
            if not isinstance(maxlen, int):
                raise TypeError('an integer is required')
            if maxlen > sys.maxsize:
                raise OverflowError('Python int too large to convert to C ssize_t')
            if maxlen < 0:
                raise ValueError('maxlen must be non-negative')
        self._maxlen = maxlen
        ring_clear(self._ring)
        deque.extend(self, iterable)

    @property
    def maxlen(self):
        """maximum size of a deque or None if unbounded"""
        return self._maxlen

    def append(self, item, /):
        """Add an element to the right side of the deque."""
        ring_push(self._ring, item, False, self._maxlen)

    def appendleft(self, item, /):
        """Add an element to the left side of the deque."""
        ring_push(self._ring, item, True, self._maxlen)

    def pop(self):
        """Remove and return the rightmost element."""
        item = ring_pop(self._ring, False)
        if item is MISSING:
            raise IndexError('pop from an empty deque')
        return item

    def popleft(self):
        """Remove and return the leftmost element."""
        item = ring_pop(self._ring, True)
        if item is MISSING:
            raise IndexError('pop from an empty deque')
        return item

    def _extend(self, iterable, left):
        """deque_extend: a list, a tuple or the deque itself is copied first,
        and its items go in at once."""
        if self._maxlen == 0:
            for _ in iterable:
                pass
            return
        ring = self._ring
        if iterable is self:
            iterable = ring_items(ring)
        if type(iterable) is list or type(iterable) is tuple:
            ring_extend(ring, iterable, left, self._maxlen)
            return
        for item in iterable:
            ring_push(ring, item, left, self._maxlen)

    def extend(self, iterable, /):
        """Extend the right side of the deque with elements from the iterable."""
        deque._extend(self, iterable, False)

    def extendleft(self, iterable, /):
        """Extend the left side of the deque with elements from the iterable."""
        deque._extend(self, iterable, True)

    def clear(self):
        """Remove all elements from the deque."""
        ring_clear(self._ring)

    def copy(self):
        """Return a shallow copy of a deque."""
        if self._maxlen is None:
            return type(self)(self)
        return type(self)(self, self._maxlen)

    def __copy__(self):
        """Return a shallow copy of a deque."""
        return self.copy()

    def _scan(self, value, start, stop, removing):
        """The first index in [start, stop) holding value, else -1. A change
        under the comparisons is a RuntimeError once nothing is found, and
        for remove() an IndexError even when something is."""
        ring = self._ring
        state = ring_state(ring)
        exc = IndexError if removing else RuntimeError
        for i in range(start, stop):
            item = ring_get(ring, i, state)
            if item is MISSING:
                raise exc('deque mutated during iteration')
            found = item is value or item == value
            if found and not removing:
                return i
            if ring_state(ring) != state:
                raise exc('deque mutated during iteration')
            if found:
                return i
        return -1

    def count(self, value, /):
        """Return number of occurrences of value."""
        ring = self._ring
        state = ring_state(ring)
        count = 0
        for i in range(ring_len(ring)):
            item = ring_get(ring, i, state)
            if item is MISSING:
                raise RuntimeError('deque mutated during iteration')
            if item is value or item == value:
                count += 1
            if ring_state(ring) != state:
                raise RuntimeError('deque mutated during iteration')
        return count

    def index(self, value, start=0, stop=sys.maxsize, /):
        """Return first index of value.

Raises ValueError if the value is not present."""
        size = ring_len(self._ring)
        start = _slice_index(start)
        stop = _slice_index(stop)
        if start < 0:
            start = max(start + size, 0)
        if stop < 0:
            stop = max(stop + size, 0)
        stop = min(stop, size)
        start = min(start, stop)
        i = deque._scan(self, value, start, stop, False)
        if i < 0:
            raise ValueError('deque.index(x): x not in deque')
        return i

    def insert(self, index, value, /):
        """Insert value before index."""
        if not ring_insert(self._ring, _index(index), value, self._maxlen):
            raise IndexError('deque already at its maximum size')

    def remove(self, value, /):
        """Remove first occurrence of value."""
        i = deque._scan(self, value, 0, ring_len(self._ring), True)
        if i < 0:
            raise ValueError('deque.remove(x): x not in deque')
        ring_del(self._ring, i)

    def reverse(self):
        """Reverse *IN PLACE*."""
        ring_reverse(self._ring)

    def rotate(self, n=1, /):
        """Rotate the deque n steps to the right.  If n is negative, rotates left."""
        ring_rotate(self._ring, _index(n))

    def __len__(self):
        return ring_len(self._ring)

    def __getitem__(self, i):
        if not isinstance(i, int) and not hasattr(type(i), '__index__'):
            raise TypeError("sequence index must be integer, not '%s'" % type(i).__name__)
        item = ring_get(self._ring, _index(i), -1)
        if item is MISSING:
            raise IndexError('deque index out of range')
        return item

    def __setitem__(self, i, value):
        if not ring_set(self._ring, _index(i), value):
            raise IndexError('deque index out of range')

    def __delitem__(self, i):
        if not ring_del(self._ring, _index(i)):
            raise IndexError('deque index out of range')

    def __contains__(self, value):
        return deque._scan(self, value, 0, ring_len(self._ring), False) >= 0

    def __iter__(self):
        return _deque_iterator(self)

    def __reversed__(self):
        """Return a reverse iterator over the deque."""
        return _deque_reverse_iterator(self)

    def __add__(self, other):
        if not isinstance(other, deque):
            raise TypeError('can only concatenate deque (not "%s") to deque' % type(other).__name__)
        new = self.copy()
        new.extend(other)
        return new

    def __iadd__(self, other):
        self.extend(other)
        return self

    def __mul__(self, n):
        new = self.copy()
        return new.__imul__(n)

    __rmul__ = __mul__

    def __imul__(self, n):
        if not isinstance(n, int) and not hasattr(type(n), '__index__'):
            raise TypeError("can't multiply sequence by non-int of type '%s'" % type(n).__name__)
        n = _index(n)
        ring = self._ring
        size = ring_len(ring)
        if not size or n == 1:
            return self
        if n <= 0:
            ring_clear(ring)
            return self
        if self._maxlen is not None and n * size > self._maxlen:
            n = (self._maxlen + size - 1) // size
        ring_extend(ring, ring_items(ring) * (n - 1), False, self._maxlen)
        return self

    def __eq__(self, other):
        if not isinstance(other, deque):
            return NotImplemented
        return ring_items(self._ring) == ring_items(other._ring)

    def __ne__(self, other):
        if not isinstance(other, deque):
            return NotImplemented
        return ring_items(self._ring) != ring_items(other._ring)

    def __lt__(self, other):
        if not isinstance(other, deque):
            return NotImplemented
        return ring_items(self._ring) < ring_items(other._ring)

    def __le__(self, other):
        if not isinstance(other, deque):
            return NotImplemented
        return ring_items(self._ring) <= ring_items(other._ring)

    def __gt__(self, other):
        if not isinstance(other, deque):
            return NotImplemented
        return ring_items(self._ring) > ring_items(other._ring)

    def __ge__(self, other):
        if not isinstance(other, deque):
            return NotImplemented
        return ring_items(self._ring) >= ring_items(other._ring)

    def __repr__(self):
        if repr_enter(self):
            return '[...]'
        try:
            body = repr(ring_items(self._ring))
        finally:
            repr_leave(self)
        if self._maxlen is None:
            return '%s(%s)' % (type(self).__name__, body)
        return '%s(%s, maxlen=%d)' % (type(self).__name__, body, self._maxlen)

    def __reduce__(self):
        """Return state information for pickling."""
        state = getattr(self, '__dict__', None) or None
        if self._maxlen is None:
            return type(self), (), state, iter(self)
        return type(self), ((), self._maxlen), state, iter(self)

    def __sizeof__(self):
        """Return the size of the deque in memory, in bytes."""
        return 64 + 8 * ring_len(self._ring)

    def __class_getitem__(cls, item):
        return GenericAlias(cls, item)


deque.__module__ = 'collections'


class _DequeIterator:
    """The state both deque iterators share: the deque, its mutation
    counter at creation, its size then and the items still to come."""

    __slots__ = ('_deque', '_state', '_size', '_counter')

    def __init__(self, deque_, index=0, /):
        if not isinstance(deque_, deque):
            raise TypeError('%s() argument 1 must be collections.deque, not %s'
                            % (type(self).__name__, type(deque_).__name__))
        self._deque = deque_
        self._state = ring_state(deque_._ring)
        self._size = self._counter = ring_len(deque_._ring)
        for _ in range(index):
            if not self._counter:
                break
            next(self)

    def __iter__(self):
        return self

    def __length_hint__(self):
        """Private method returning an estimate of len(list(it))."""
        return self._counter

    def __reduce__(self):
        """Return state information for pickling."""
        return type(self), (self._deque, ring_len(self._deque._ring) - self._counter)

    def _stale(self):
        self._counter = 0
        raise RuntimeError('deque mutated during iteration')


class _deque_iterator(_DequeIterator):
    __slots__ = ()

    def __next__(self):
        if not self._counter:
            if ring_state(self._deque._ring) != self._state:
                self._stale()
            raise StopIteration
        item = ring_get(self._deque._ring, self._size - self._counter, self._state)
        if item is MISSING:
            self._stale()
        self._counter -= 1
        return item


class _deque_reverse_iterator(_DequeIterator):
    __slots__ = ()

    def __next__(self):
        if not self._counter:
            raise StopIteration
        item = ring_get(self._deque._ring, self._counter - 1, self._state)
        if item is MISSING:
            self._stale()
        self._counter -= 1
        return item


_deque_iterator.__module__ = _deque_reverse_iterator.__module__ = 'collections'


class defaultdict(dict):
    """defaultdict(default_factory=None, /, [...]) --> dict with default factory

The default factory is called without arguments to produce
a new value when a key is not present, in __getitem__ only.
A defaultdict compares equal to a dict with the same items.
All remaining arguments are treated the same as if they were
passed to the dict constructor, including keyword arguments.
"""

    __slots__ = ('default_factory',)

    def __init__(self, default_factory=None, /, *args, **kwargs):
        if default_factory is not None and not callable(default_factory):
            raise TypeError('first argument must be callable or None')
        dict.__init__(self, *args, **kwargs)
        self.default_factory = default_factory

    def __missing__(self, key, /):
        if self.default_factory is None:
            raise KeyError(key)
        return dict.setdefault(self, key, self.default_factory())

    def __repr__(self):
        base = dict.__repr__(self)
        factory = self.default_factory
        if factory is None:
            shown = 'None'
        elif repr_enter(factory):
            shown = '...'
        else:
            try:
                shown = repr(factory)
            finally:
                repr_leave(factory)
        return '%s(%s, %s)' % (type(self).__name__, shown, base)

    def copy(self):
        """D.copy() -> a shallow copy of D."""
        return type(self)(self.default_factory, self)

    __copy__ = copy

    def __reduce__(self):
        """Return state information for pickling."""
        args = () if self.default_factory is None else (self.default_factory,)
        return type(self), args, None, None, iter(self.items())

    def __or__(self, other):
        if not isinstance(other, dict):
            return NotImplemented
        new = type(self)(self.default_factory, self)
        new.update(other)
        return new

    def __ror__(self, other):
        if not isinstance(other, dict):
            return NotImplemented
        new = type(self)(self.default_factory, other)
        new.update(self)
        return new


defaultdict.__module__ = 'collections'


def _count_elements(mapping, iterable, /):
    """Count elements in the iterable, updating the mapping"""
    it = iter(iterable)
    get = mapping.get
    for elem in it:
        mapping[elem] = get(elem, 0) + 1


class _tuplegetter:
    __slots__ = ('_index', '__doc__')

    def __new__(cls, index, doc, /):
        self = object.__new__(cls)
        self._index = _index(index)
        self.__doc__ = doc
        return self

    def __get__(self, instance, owner=None, /):
        if instance is None:
            return self
        if not kind(instance, 'tuple'):
            raise TypeError("descriptor for index '%d' for tuple subclasses doesn't apply to '%s' object"
                            % (self._index, type(instance).__name__))
        if self._index >= size(instance):
            raise IndexError('tuple index out of range')
        return seq_get(instance, self._index)

    def __set__(self, instance, value, /):
        raise AttributeError("can't set attribute")

    def __delete__(self, instance, /):
        raise AttributeError("can't delete attribute")

    def __reduce__(self):
        return type(self), (self._index, self.__doc__)


_tuplegetter.__module__ = 'collections'
kernel_set('tuplegetter', _tuplegetter)
