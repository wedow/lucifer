"""_collections (Modules/_collectionsmodule.c): deque over a list,
defaultdict, _count_elements and _tuplegetter. OrderedDict stays the
pure-Python fallback of collections."""

import sys
from _lucifer import repr_enter, repr_leave, kind, size, seq_get
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

    __slots__ = ('_items', '_maxlen', '_state', '__weakref__')
    __hash__ = None

    def __new__(cls, *args, **kwargs):
        self = object.__new__(cls)
        self._items = []
        self._maxlen = None
        self._state = 0
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
        self.clear()
        self.extend(iterable)

    @property
    def maxlen(self):
        """maximum size of a deque or None if unbounded"""
        return self._maxlen

    def append(self, item, /):
        """Add an element to the right side of the deque."""
        items = self._items
        items.append(item)
        if self._maxlen is not None and len(items) > self._maxlen:
            del items[0]
        self._state += 1

    def appendleft(self, item, /):
        """Add an element to the left side of the deque."""
        items = self._items
        items.insert(0, item)
        if self._maxlen is not None and len(items) > self._maxlen:
            del items[-1]
        self._state += 1

    def pop(self):
        """Remove and return the rightmost element."""
        if not self._items:
            raise IndexError('pop from an empty deque')
        self._state += 1
        return self._items.pop()

    def popleft(self):
        """Remove and return the leftmost element."""
        if not self._items:
            raise IndexError('pop from an empty deque')
        self._state += 1
        return self._items.pop(0)

    def extend(self, iterable, /):
        """Extend the right side of the deque with elements from the iterable."""
        if iterable is self:
            iterable = list(self._items)
        it = iter(iterable)
        if self._maxlen == 0:
            for _ in it:
                pass
            return
        for item in it:
            deque.append(self, item)

    def extendleft(self, iterable, /):
        """Extend the left side of the deque with elements from the iterable."""
        if iterable is self:
            iterable = list(self._items)
        it = iter(iterable)
        if self._maxlen == 0:
            for _ in it:
                pass
            return
        for item in it:
            deque.appendleft(self, item)

    def clear(self):
        """Remove all elements from the deque."""
        if self._items:
            self._state += 1
            self._items.clear()

    def copy(self):
        """Return a shallow copy of a deque."""
        if self._maxlen is None:
            return type(self)(self)
        return type(self)(self, self._maxlen)

    def __copy__(self):
        """Return a shallow copy of a deque."""
        return self.copy()

    def count(self, value, /):
        """Return number of occurrences of value."""
        items = self._items
        state = self._state
        count = 0
        for i in range(len(items)):
            item = items[i]
            if item is value or item == value:
                count += 1
            if state != self._state:
                raise RuntimeError('deque mutated during iteration')
        return count

    def index(self, value, start=0, stop=sys.maxsize, /):
        """Return first index of value.

Raises ValueError if the value is not present."""
        items = self._items
        state = self._state
        size = len(items)
        start = _slice_index(start)
        stop = _slice_index(stop)
        if start < 0:
            start = max(start + size, 0)
        if stop < 0:
            stop = max(stop + size, 0)
        stop = min(stop, size)
        start = min(start, stop)
        for i in range(start, stop):
            item = items[i]
            if item is value or item == value:
                return i
            if state != self._state:
                raise RuntimeError('deque mutated during iteration')
        raise ValueError('deque.index(x): x not in deque')

    def insert(self, index, value, /):
        """Insert value before index."""
        index = _index(index)
        if self._maxlen == len(self._items):
            raise IndexError('deque already at its maximum size')
        self._items.insert(index, value)
        self._state += 1

    def remove(self, value, /):
        """Remove first occurrence of value."""
        items = self._items
        state = self._state
        for i in range(len(items)):
            item = items[i]
            found = item is value or item == value
            if state != self._state:
                raise IndexError('deque mutated during iteration')
            if found:
                del items[i]
                self._state += 1
                return
        raise ValueError('deque.remove(x): x not in deque')

    def reverse(self):
        """Reverse *IN PLACE*."""
        self._items.reverse()

    def rotate(self, n=1, /):
        """Rotate the deque n steps to the right.  If n is negative, rotates left."""
        n = _index(n)
        items = self._items
        if len(items) > 1:
            self._state += 1
            n %= len(items)
            if n:
                items[:] = items[-n:] + items[:-n]

    def __len__(self):
        return len(self._items)

    def __getitem__(self, i):
        if not isinstance(i, int) and not hasattr(type(i), '__index__'):
            raise TypeError("sequence index must be integer, not '%s'" % type(i).__name__)
        i = _index(i)
        if not -len(self._items) <= i < len(self._items):
            raise IndexError('deque index out of range')
        return self._items[i]

    def __setitem__(self, i, value):
        i = _index(i)
        if not -len(self._items) <= i < len(self._items):
            raise IndexError('deque index out of range')
        self._items[i] = value

    def __delitem__(self, i):
        i = _index(i)
        if not -len(self._items) <= i < len(self._items):
            raise IndexError('deque index out of range')
        del self._items[i]
        self._state += 1

    def __contains__(self, value):
        items = self._items
        state = self._state
        for i in range(len(items)):
            item = items[i]
            if item is value or item == value:
                return True
            if state != self._state:
                raise RuntimeError('deque mutated during iteration')
        return False

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
        items = self._items
        if not items or n == 1:
            return self
        if n <= 0:
            self.clear()
        elif len(items) == 1:
            if self._maxlen is not None:
                n = min(n, self._maxlen)
            self._state += 1
            items.extend(items * (n - 1))
        else:
            seq = list(items)
            if self._maxlen is not None and n * len(items) > self._maxlen:
                n = (self._maxlen + len(items) - 1) // len(items)
            for _ in range(n - 1):
                self.extend(seq)
        return self

    def __eq__(self, other):
        if not isinstance(other, deque):
            return NotImplemented
        return self._items == other._items

    def __ne__(self, other):
        if not isinstance(other, deque):
            return NotImplemented
        return self._items != other._items

    def __lt__(self, other):
        if not isinstance(other, deque):
            return NotImplemented
        return self._items < other._items

    def __le__(self, other):
        if not isinstance(other, deque):
            return NotImplemented
        return self._items <= other._items

    def __gt__(self, other):
        if not isinstance(other, deque):
            return NotImplemented
        return self._items > other._items

    def __ge__(self, other):
        if not isinstance(other, deque):
            return NotImplemented
        return self._items >= other._items

    def __repr__(self):
        if repr_enter(self):
            return '[...]'
        try:
            body = repr(list(self._items))
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
        return 64 + 8 * len(self._items)

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
        self._state = deque_._state
        self._size = self._counter = len(deque_._items)
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
        return type(self), (self._deque, len(self._deque._items) - self._counter)

    def _stale(self):
        self._counter = 0
        raise RuntimeError('deque mutated during iteration')


class _deque_iterator(_DequeIterator):
    __slots__ = ()

    def __next__(self):
        if self._deque._state != self._state:
            self._stale()
        if not self._counter:
            raise StopIteration
        item = self._deque._items[self._size - self._counter]
        self._counter -= 1
        return item


class _deque_reverse_iterator(_DequeIterator):
    __slots__ = ()

    def __next__(self):
        if not self._counter:
            raise StopIteration
        if self._deque._state != self._state:
            self._stale()
        self._counter -= 1
        return self._deque._items[self._counter]


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
