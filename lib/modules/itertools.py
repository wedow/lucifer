"""itertools (Modules/itertoolsmodule.c) as the documented Python
equivalents. Each iterator is a class with the state in slots; the
combinatorics run in a generator held by the iterator."""

import sys
from _types import GenericAlias

_MISSING = object()


def _index(x):
    """PyNumber_AsSsize_t: an int or an object with __index__."""
    if isinstance(x, int):
        return x
    try:
        method = type(x).__index__
    except AttributeError:
        raise TypeError("'%s' object cannot be interpreted as an integer" % type(x).__name__) from None
    return method(x)


def _self_iter(self):
    return self


def _gen_next(self):
    return next(self._gen)


class chain:
    """Return a chain object whose .__next__() method returns elements from the
first iterable until it is exhausted, then elements from the next
iterable, until all of the iterables are exhausted."""

    __module__ = 'itertools'
    __slots__ = ('_source', '_active')

    def __new__(cls, *iterables):
        return _chain(cls, iter(iterables))

    @classmethod
    def from_iterable(cls, iterable, /):
        """Alternative chain() constructor taking a single iterable argument that evaluates lazily."""
        return _chain(cls, iter(iterable))

    __iter__ = _self_iter

    def __next__(self):
        while self._source is not None:
            if self._active is None:
                try:
                    iterable = next(self._source)
                except StopIteration:
                    self._source = None
                    raise
                self._active = iter(iterable)
            try:
                return next(self._active)
            except StopIteration:
                self._active = None
        raise StopIteration

    def __class_getitem__(cls, item):
        return GenericAlias(cls, item)


def _chain(cls, source):
    self = object.__new__(cls)
    self._source = source
    self._active = None
    return self


class repeat:
    """repeat(object [,times]) -> create an iterator which returns the object
for the specified number of times.  If not specified, returns the object
endlessly."""

    __module__ = 'itertools'
    __slots__ = ('_object', '_times')

    def __new__(cls, object, times=None):
        self = object_new(cls)
        self._object = object
        self._times = None if times is None else max(_index(times), 0)
        return self

    __iter__ = _self_iter

    def __next__(self):
        if self._times is not None:
            if self._times <= 0:
                raise StopIteration
            self._times -= 1
        return self._object

    def __length_hint__(self):
        """Private method returning an estimate of len(list(it))."""
        if self._times is None:
            raise TypeError('len() of unsized object')
        return self._times

    def __repr__(self):
        if self._times is None:
            return '%s(%r)' % (type(self).__name__, self._object)
        return '%s(%r, %d)' % (type(self).__name__, self._object, self._times)


object_new = object.__new__


class starmap:
    """Return an iterator whose values are returned from the function evaluated
with an argument tuple taken from the given sequence."""

    __module__ = 'itertools'
    __slots__ = ('_function', '_it')

    def __new__(cls, function, iterable, /):
        self = object_new(cls)
        self._function = function
        self._it = iter(iterable)
        return self

    __iter__ = _self_iter

    def __next__(self):
        return self._function(*tuple(next(self._it)))


def _ssize(x):
    """PyNumber_AsSsize_t as islice reads it: -1 when x is no usable index."""
    try:
        n = _index(x)
    except (TypeError, OverflowError):
        return -1
    return n if n <= sys.maxsize else -1


class islice:
    """islice(iterable, stop) --> islice object
islice(iterable, start, stop[, step]) --> islice object

Return an iterator whose next() method returns selected values from an
iterable.  If start is specified, will skip all preceding elements;
otherwise, start defaults to zero.  Step defaults to one.  If
specified as another value, step determines how many values are
skipped between successive calls.  Works like a slice() on a list
but returns an iterator."""

    __module__ = 'itertools'
    __slots__ = ('_it', '_next', '_stop', '_step', '_cnt')

    def __new__(cls, iterable, *args):
        if not 1 <= len(args) <= 3:
            raise TypeError('islice expected %s, got %d'
                            % ('at most 4 arguments' if args else 'at least 2 arguments', len(args) + 1))
        start, stop, step = 0, -1, 1
        stop_error = ValueError('Stop argument for islice() must be None or an integer: 0 <= x <= sys.maxsize.')
        if len(args) == 1:
            if args[0] is not None:
                stop = _ssize(args[0])
                if stop == -1:
                    raise stop_error
        else:
            if args[0] is not None:
                start = _ssize(args[0])
            if args[1] is not None:
                stop = _ssize(args[1])
                if stop == -1:
                    raise stop_error
        if start < 0 or stop < -1:
            raise ValueError('Indices for islice() must be None or an integer: 0 <= x <= sys.maxsize.')
        if len(args) == 3 and args[2] is not None:
            step = _ssize(args[2])
        if step < 1:
            raise ValueError('Step for islice() must be a positive integer or None.')
        self = object_new(cls)
        self._it = iter(iterable)
        self._next = start
        self._stop = stop
        self._step = step
        self._cnt = 0
        return self

    __iter__ = _self_iter

    def __next__(self):
        it = self._it
        if it is None:
            raise StopIteration
        try:
            while self._cnt < self._next:
                next(it)
                self._cnt += 1
            if self._stop != -1 and self._cnt >= self._stop:
                raise StopIteration
            item = next(it)
        except StopIteration:
            self._it = None
            raise
        self._cnt += 1
        self._next += self._step
        if self._stop != -1 and self._next > self._stop:
            self._next = self._stop
        return item


def _is_number(x):
    return isinstance(x, (int, float, complex)) or any(
        hasattr(type(x), name) for name in ('__index__', '__int__', '__float__'))


class count:
    """Return a count object whose .__next__() method returns consecutive values.

Equivalent to:
    def count(firstval=0, step=1):
        x = firstval
        while 1:
            yield x
            x += step"""

    __module__ = 'itertools'
    __slots__ = ('_cnt', '_step')

    def __new__(cls, start=0, step=1):
        if not _is_number(start) or not _is_number(step):
            raise TypeError('a number is required')
        self = object_new(cls)
        self._cnt = int(start) if isinstance(start, int) else start
        self._step = int(step) if isinstance(step, int) else step
        return self

    __iter__ = _self_iter

    def __next__(self):
        value = self._cnt
        self._cnt = value + self._step
        return value

    def __repr__(self):
        if isinstance(self._step, int) and self._step == 1:
            return '%s(%r)' % (type(self).__name__, self._cnt)
        return '%s(%r, %r)' % (type(self).__name__, self._cnt, self._step)


class cycle:
    """Return elements from the iterable until it is exhausted. Then repeat the sequence indefinitely."""

    __module__ = 'itertools'
    __slots__ = ('_it', '_saved', '_pos')

    def __new__(cls, iterable, /):
        self = object_new(cls)
        self._it = iter(iterable)
        self._saved = []
        self._pos = 0
        return self

    __iter__ = _self_iter

    def __next__(self):
        if self._it is not None:
            try:
                item = next(self._it)
            except StopIteration:
                self._it = None
            else:
                self._saved.append(item)
                return item
        if not self._saved:
            raise StopIteration
        item = self._saved[self._pos]
        self._pos = (self._pos + 1) % len(self._saved)
        return item


class accumulate:
    """Return series of accumulated sums (or other binary function results)."""

    __module__ = 'itertools'
    __slots__ = ('_it', '_func', '_total', '_yield_total')

    def __new__(cls, iterable, func=None, *, initial=None):
        self = object_new(cls)
        self._it = iter(iterable)
        self._func = func
        self._total = _MISSING if initial is None else initial
        self._yield_total = initial is not None
        return self

    __iter__ = _self_iter

    def __next__(self):
        if self._yield_total:
            self._yield_total = False
            return self._total
        item = next(self._it)
        if self._total is _MISSING:
            self._total = item
        elif self._func is None:
            self._total = self._total + item
        else:
            self._total = self._func(self._total, item)
        return self._total


class compress:
    """Return data elements corresponding to true selector elements.

Forms a shorter iterator from selected data elements using the selectors to
choose the data elements."""

    __module__ = 'itertools'
    __slots__ = ('_data', '_selectors')

    def __new__(cls, data, selectors):
        self = object_new(cls)
        self._data = iter(data)
        self._selectors = iter(selectors)
        return self

    __iter__ = _self_iter

    def __next__(self):
        while True:
            datum = next(self._data)
            if next(self._selectors):
                return datum


class dropwhile:
    """Drop items from the iterable while predicate(item) is true.

Afterwards, return every element until the iterable is exhausted."""

    __module__ = 'itertools'
    __slots__ = ('_predicate', '_it', '_dropping')

    def __new__(cls, predicate, iterable, /):
        self = object_new(cls)
        self._predicate = predicate
        self._it = iter(iterable)
        self._dropping = True
        return self

    __iter__ = _self_iter

    def __next__(self):
        item = next(self._it)
        if self._dropping:
            while self._predicate(item):
                item = next(self._it)
            self._dropping = False
        return item


class takewhile:
    """Return successive entries from an iterable as long as the predicate evaluates to true for each entry."""

    __module__ = 'itertools'
    __slots__ = ('_predicate', '_it', '_taking')

    def __new__(cls, predicate, iterable, /):
        self = object_new(cls)
        self._predicate = predicate
        self._it = iter(iterable)
        self._taking = True
        return self

    __iter__ = _self_iter

    def __next__(self):
        if not self._taking:
            raise StopIteration
        item = next(self._it)
        if self._predicate(item):
            return item
        self._taking = False
        raise StopIteration


class filterfalse:
    """Return those items of iterable for which function(item) is false.

If function is None, return the items that are false."""

    __module__ = 'itertools'
    __slots__ = ('_predicate', '_it')

    def __new__(cls, function, iterable, /):
        self = object_new(cls)
        self._predicate = bool if function is None else function
        self._it = iter(iterable)
        return self

    __iter__ = _self_iter

    def __next__(self):
        while True:
            item = next(self._it)
            if not self._predicate(item):
                return item


class zip_longest:
    """Return a zip_longest object whose .__next__() method returns a tuple where
the i-th element comes from the i-th iterable argument.  The .__next__()
method continues until the longest iterable in the argument sequence
is exhausted and then it raises StopIteration.  When the shorter iterables
are exhausted, the fillvalue is substituted in their place.  The fillvalue
defaults to None or can be specified by a keyword argument."""

    __module__ = 'itertools'
    __slots__ = ('_its', '_active', '_fillvalue')

    def __new__(cls, *iterables, fillvalue=None):
        self = object_new(cls)
        self._its = [iter(iterable) for iterable in iterables]
        self._active = len(self._its)
        self._fillvalue = fillvalue
        return self

    __iter__ = _self_iter

    def __next__(self):
        if not self._active:
            raise StopIteration
        result = []
        for i, it in enumerate(self._its):
            try:
                if it is None:
                    raise StopIteration
                result.append(next(it))
            except StopIteration:
                if it is not None:
                    self._active -= 1
                    self._its[i] = None
                    if not self._active:
                        raise
                result.append(self._fillvalue)
        return tuple(result)


class pairwise:
    """Return an iterator of overlapping pairs taken from the input iterator.

    s -> (s0,s1), (s1,s2), (s2, s3), ..."""

    __module__ = 'itertools'
    __slots__ = ('_it', '_old')

    def __new__(cls, iterable, /):
        self = object_new(cls)
        self._it = iter(iterable)
        self._old = _MISSING
        return self

    __iter__ = _self_iter

    def __next__(self):
        if self._it is None:
            raise StopIteration
        try:
            if self._old is _MISSING:
                self._old = next(self._it)
            new = next(self._it)
        except StopIteration:
            self._it = None
            self._old = _MISSING
            raise
        pair = (self._old, new)
        self._old = new
        return pair


class batched:
    """Batch data into tuples of length n. The last batch may be shorter than n.

Loops over the input iterable and accumulates data into tuples
up to size n.  The input is consumed lazily, just enough to
fill a batch.  The result is yielded as soon as a batch is full
or when the input iterable is exhausted.

If strict is true, raises a ValueError if the final batch is shorter than n."""

    __module__ = 'itertools'
    __slots__ = ('_it', '_n', '_strict')

    def __new__(cls, iterable, n, *, strict=False):
        n = _index(n)
        if n < 1:
            raise ValueError('n must be at least one')
        self = object_new(cls)
        self._it = iter(iterable)
        self._n = n
        self._strict = strict
        return self

    __iter__ = _self_iter

    def __next__(self):
        if self._it is None:
            raise StopIteration
        batch = []
        try:
            while len(batch) < self._n:
                batch.append(next(self._it))
        except StopIteration:
            self._it = None
            if not batch:
                raise
            if self._strict:
                raise ValueError('batched(): incomplete batch') from None
        return tuple(batch)


def _product(pools):
    result = [[]]
    for pool in pools:
        result = [x + [y] for x in result for y in pool]
    for prod in result:
        yield tuple(prod)


def _permutations(pool, r):
    n = len(pool)
    if r > n:
        return
    indices = list(range(n))
    cycles = list(range(n, n - r, -1))
    yield tuple(pool[i] for i in indices[:r])
    while n:
        for i in reversed(range(r)):
            cycles[i] -= 1
            if cycles[i] == 0:
                indices[i:] = indices[i + 1:] + indices[i:i + 1]
                cycles[i] = n - i
            else:
                j = cycles[i]
                indices[i], indices[-j] = indices[-j], indices[i]
                yield tuple(pool[i] for i in indices[:r])
                break
        else:
            return


def _combinations(pool, r):
    n = len(pool)
    if r > n:
        return
    indices = list(range(r))
    yield tuple(pool[i] for i in indices)
    while True:
        for i in reversed(range(r)):
            if indices[i] != i + n - r:
                break
        else:
            return
        indices[i] += 1
        for j in range(i + 1, r):
            indices[j] = indices[j - 1] + 1
        yield tuple(pool[i] for i in indices)


def _combinations_with_replacement(pool, r):
    n = len(pool)
    if not n and r:
        return
    indices = [0] * r
    yield tuple(pool[i] for i in indices)
    while True:
        for i in reversed(range(r)):
            if indices[i] != n - 1:
                break
        else:
            return
        indices[i:] = [indices[i] + 1] * (r - i)
        yield tuple(pool[i] for i in indices)


def _generated(cls, gen):
    self = object_new(cls)
    self._gen = gen
    return self


def _choose_size(r, n):
    if r is None:
        r = n
    elif not isinstance(r, int):
        raise TypeError('Expected int as r')
    if r < 0:
        raise ValueError('r must be non-negative')
    return r


class product:
    """Cartesian product of input iterables.  Equivalent to nested for-loops.

For example, product(A, B) returns the same as:  ((x,y) for x in A for y in B).
The leftmost iterators are in the outermost for-loop, so the output tuples
cycle in a manner similar to an odometer (with the rightmost element changing
on every iteration).

To compute the product of an iterable with itself, specify the number
of repetitions with the optional repeat keyword argument. For example,
product(A, repeat=4) means the same as product(A, A, A, A)."""

    __module__ = 'itertools'
    __slots__ = ('_gen',)

    def __new__(cls, *iterables, repeat=1):
        repeat = _index(repeat)
        if repeat < 0:
            raise ValueError('repeat argument cannot be negative')
        return _generated(cls, _product([tuple(it) for it in iterables] * repeat))

    __iter__ = _self_iter
    __next__ = _gen_next


class permutations:
    """Return successive r-length permutations of elements in the iterable.

permutations(range(3), 2) --> (0,1), (0,2), (1,0), (1,2), (2,0), (2,1)"""

    __module__ = 'itertools'
    __slots__ = ('_gen',)

    def __new__(cls, iterable, r=None):
        pool = tuple(iterable)
        return _generated(cls, _permutations(pool, _choose_size(r, len(pool))))

    __iter__ = _self_iter
    __next__ = _gen_next


class combinations:
    """Return successive r-length combinations of elements in the iterable.

combinations(range(4), 3) --> (0,1,2), (0,1,3), (0,2,3), (1,2,3)"""

    __module__ = 'itertools'
    __slots__ = ('_gen',)

    def __new__(cls, iterable, r):
        pool = tuple(iterable)
        return _generated(cls, _combinations(pool, _choose_size(r, len(pool))))

    __iter__ = _self_iter
    __next__ = _gen_next


class combinations_with_replacement:
    """Return successive r-length combinations of elements in the iterable allowing individual elements to have successive repeats.

combinations_with_replacement('ABC', 2) --> ('A','A'), ('A','B'), ('A','C'), ('B','B'), ('B','C'), ('C','C')"""

    __module__ = 'itertools'
    __slots__ = ('_gen',)

    def __new__(cls, iterable, r):
        pool = tuple(iterable)
        return _generated(cls, _combinations_with_replacement(pool, _choose_size(r, len(pool))))

    __iter__ = _self_iter
    __next__ = _gen_next


class groupby:
    """make an iterator that returns consecutive keys and groups from the iterable

  iterable
    Elements to divide into groups according to the key function.
  key
    A function for computing the group category for each element.
    If the key function is not specified or is None, the element itself
    is used for grouping."""

    __module__ = 'itertools'
    __slots__ = ('_it', '_keyfunc', '_tgtkey', '_currkey', '_currvalue', '_currgrouper')

    def __new__(cls, iterable, key=None):
        self = object_new(cls)
        self._it = iter(iterable)
        self._keyfunc = key
        self._tgtkey = self._currkey = self._currvalue = _MISSING
        self._currgrouper = None
        return self

    __iter__ = _self_iter

    def _step(self):
        value = next(self._it)
        self._currkey = value if self._keyfunc is None else self._keyfunc(value)
        self._currvalue = value

    def __next__(self):
        self._currgrouper = None
        while True:
            if self._currkey is _MISSING:
                pass
            elif self._tgtkey is _MISSING:
                break
            else:
                same = self._tgtkey is self._currkey or self._tgtkey == self._currkey
                if not same:
                    break
            self._step()
        self._tgtkey = self._currkey
        grouper = _grouper(self, self._tgtkey)
        self._currgrouper = grouper
        return self._currkey, grouper


class _grouper:
    __module__ = 'itertools'
    __slots__ = ('_parent', '_tgtkey')

    def __init__(self, parent, tgtkey):
        self._parent = parent
        self._tgtkey = tgtkey

    def __iter__(self):
        return self

    def __next__(self):
        parent = self._parent
        if parent._currgrouper is not self:
            raise StopIteration
        if parent._currvalue is _MISSING:
            parent._step()
        if not (self._tgtkey is parent._currkey or self._tgtkey == parent._currkey):
            raise StopIteration
        value = parent._currvalue
        parent._currvalue = _MISSING
        return value


class _tee:
    """Iterator wrapped to make it copyable."""

    __module__ = 'itertools'
    __slots__ = ('_it', '_link')

    def __init__(self, iterable, /):
        self._it = iter(iterable)
        self._link = [_MISSING, None]

    def __iter__(self):
        return self

    def __next__(self):
        link = self._link
        if link[1] is None:
            link[0] = next(self._it)
            link[1] = [_MISSING, None]
        self._link = link[1]
        return link[0]

    def __copy__(self):
        new = object_new(type(self))
        new._it = self._it
        new._link = self._link
        return new


def tee(iterable, n=2, /):
    """Returns a tuple of n independent iterators."""
    n = _index(n)
    if n < 0:
        raise ValueError('n must be >= 0')
    it = iter(iterable)
    if not n:
        return ()
    if not hasattr(it, '__copy__'):
        it = _tee(it)
    return (it,) + tuple(it.__copy__() for _ in range(n - 1))
