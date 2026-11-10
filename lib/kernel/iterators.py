"""map, filter, zip, enumerate and reversed (Python/bltinmodule.c,
Objects/enumobject.c). Each instance keeps its state in the three object
fields of its native layout; the kernel reaches them by index."""

from _lucifer import (
    MISSING, error, str_join, kind, size, type_lookup, type_tp_name, static_type, alloc,
    field_get, field_set, iter_new, tuple_from, list_append, kernel_twin,
)
from _lucifer import _boot, operators, _args, iter as _iter

kernel_type = _boot.kernel_type
call_special = _boot.call_special
decimal = operators.decimal
index = operators.index
truth = operators.truth
get_iter = _iter.get_iter
next_item = _iter.next_item

__all__ = ()


def _name(x):
    return type_tp_name(type(x))


def _strict_option(fname, kwargs):
    """The `strict` keyword of map and zip, parsed as "|$p"."""
    if not kwargs:
        return False
    strict = False
    for key, value in kwargs.items():
        if key != 'strict':
            error(TypeError, str_join('', (fname, "() got an unexpected keyword argument '", key, "'")))
        strict = True if truth(value) else False
    return strict


def _lengths(fname, i, shorter):
    """The ValueError of a strict iteration whose argument i + 1 ended
    earlier (shorter) or later than the arguments before it."""
    error(ValueError, str_join('', (
        fname, '() argument ', decimal(i + 1), ' is ', 'shorter' if shorter else 'longer',
        ' than argument', ' ' if i == 1 else 's 1-', decimal(i))))


def _check_strict(fname, iters, i):
    """The strict ending after the iterator at i ended: the ones before it
    ended too, and every later one ends as well; else a ValueError."""
    if i:
        _lengths(fname, i, True)
    for j in range(1, size(iters)):
        if next_item(iters[j]) is not MISSING:
            _lengths(fname, j, False)


@kernel_type(static_type('map'))
class map:
    """Make an iterator that computes the function using arguments from
each of the iterables.  Stops when the shortest iterable is exhausted.

If strict is true and one of the arguments is exhausted before the others,
raise a ValueError."""

    def __new__(cls, /, *args, **kwargs):
        if cls is not map:
            _boot.check_new(cls, map)
        strict = _strict_option('map', kwargs)
        if size(args) < 2:
            error(TypeError, 'map() must have at least two arguments.')
        iters = []
        for arg in args[1:]:
            iters = [*iters, get_iter(arg)]
        self = alloc(cls, map)
        field_set(self, 0, tuple_from(iters, tuple))
        field_set(self, 1, args[0])
        field_set(self, 2, strict)
        return self

    def __iter__(self):
        return self

    def __next__(self):
        v = _map_next(self)
        if v is MISSING:
            raise StopIteration
        return v


def _map_next(self):
    iters = field_get(self, 0)
    values = []
    for i in range(size(iters)):
        v = next_item(iters[i])
        if v is MISSING:
            if field_get(self, 2):
                _check_strict('map', iters, i)
            return MISSING
        values = [*values, v]
    return field_get(self, 1)(*values)


def _filter_next(self):
    func = field_get(self, 0)
    it = field_get(self, 1)
    while True:
        item = next_item(it)
        if item is MISSING:
            return MISSING
        if func is None or func is bool:
            ok = truth(item)
        else:
            ok = truth(func(item))
        if ok:
            return item


@kernel_type(static_type('filter'))
class filter:
    """Return an iterator yielding those items of iterable for which function(item)
is true. If function is None, return the items that are true."""

    def __new__(cls, /, *args, **kwargs):
        if cls is not filter:
            _boot.check_new(cls, filter)
        if kwargs and (cls is filter or type_lookup(cls, '__init__') is type_lookup(filter, '__init__')):
            error(TypeError, 'filter() takes no keyword arguments')
        if size(args) != 2:
            error(TypeError, str_join('', ('filter expected 2 arguments, got ', decimal(size(args)))))
        it = get_iter(args[1])
        self = alloc(cls, filter)
        field_set(self, 0, args[0])
        field_set(self, 1, it)
        return self

    def __iter__(self):
        return self

    def __next__(self):
        v = _filter_next(self)
        if v is MISSING:
            raise StopIteration
        return v


def _zip_new2(cls, a, b):
    """zip(a, b) (kernel_twin): the common zip, without the argument parsing."""
    if cls is not zip:
        _boot.check_new(cls, zip)
    self = alloc(cls, zip)
    field_set(self, 0, (get_iter(a), get_iter(b)))
    field_set(self, 2, False)
    return self


def _zip_next(self):
    """The next tuple of a zip, or MISSING at its end. Two iterators, the
    common zip, make no list."""
    iters = field_get(self, 0)
    n = size(iters)
    if n == 2:
        a = next_item(iters[0])
        if a is MISSING:
            if field_get(self, 2):
                _check_strict('zip', iters, 0)
            return MISSING
        b = next_item(iters[1])
        if b is MISSING:
            if field_get(self, 2):
                _check_strict('zip', iters, 1)
            return MISSING
        return (a, b)
    if not n:
        return MISSING
    values = []
    for i in range(size(iters)):
        v = next_item(iters[i])
        if v is MISSING:
            if field_get(self, 2):
                _check_strict('zip', iters, i)
            return MISSING
        list_append(values, v)
    return tuple_from(values, tuple)


def _enumerate_next(self):
    """The next (index, item) of an enumerate, or MISSING at its end."""
    item = next_item(field_get(self, 0))
    if item is MISSING:
        return MISSING
    i = field_get(self, 1)
    field_set(self, 1, i + 1)
    return (i, item)


@kernel_type(static_type('zip'))
class zip:
    """The zip object yields n-length tuples, where n is the number of iterables
passed as positional arguments to zip().  The i-th element in every tuple
comes from the i-th iterable argument to zip().  This continues until the
shortest argument is exhausted.

If strict is true and one of the arguments is exhausted before the others,
raise a ValueError."""

    def __new__(cls, /, *args, **kwargs):
        if cls is not zip:
            _boot.check_new(cls, zip)
        strict = _strict_option('zip', kwargs)
        iters = []
        for arg in args:
            iters = [*iters, get_iter(arg)]
        self = alloc(cls, zip)
        field_set(self, 0, tuple_from(iters, tuple))
        field_set(self, 2, strict)
        return self

    kernel_twin(__new__, _zip_new2)

    def __iter__(self):
        return self

    def __next__(self):
        v = _zip_next(self)
        if v is MISSING:
            raise StopIteration
        return v


@kernel_type(static_type('enumerate'))
class enumerate:
    """Return an enumerate object.

  iterable
    an object supporting iteration

The enumerate object yields pairs containing a count (from start, which
defaults to zero) and a value yielded by the iterable argument.

enumerate is useful for obtaining an indexed list:
    (0, seq[0]), (1, seq[1]), (2, seq[2]), ..."""

    def __new__(cls, /, *args, **kwargs):
        if cls is not enumerate:
            _boot.check_new(cls, enumerate)
        if not args and not kwargs:
            error(TypeError, "enumerate() missing required argument 'iterable'")
        if size(args) + size(kwargs) == 2:
            for key, _ in kwargs.items():
                if key != 'start' and key != 'iterable':
                    error(TypeError, str_join('', (
                        "'", key, "' is an invalid keyword argument for enumerate()")))
        iterable, start = _args.unpack('enumerate', args, kwargs, ('iterable', 'start'), 0, 1)
        start = 0 if start is MISSING else index(start)
        it = get_iter(iterable)
        self = alloc(cls, enumerate)
        field_set(self, 0, it)
        field_set(self, 1, start)
        return self

    def __iter__(self):
        return self

    def __next__(self):
        v = _enumerate_next(self)
        if v is MISSING:
            raise StopIteration
        return v


@kernel_type(static_type('reversed'))
class reversed:
    """Return a reverse iterator over the values of the given sequence."""

    def __new__(cls, /, *args, **kwargs):
        if cls is not reversed:
            _boot.check_new(cls, reversed)
        if kwargs and (cls is reversed or type_lookup(cls, '__init__') is type_lookup(reversed, '__init__')):
            error(TypeError, 'reversed() takes no keyword arguments')
        if size(args) != 1:
            error(TypeError, str_join('', ('reversed expected 1 argument, got ', decimal(size(args)))))
        seq = args[0]
        meth = type_lookup(type(seq), '__reversed__')
        if meth is None:
            error(TypeError, str_join('', ("'", _name(seq), "' object is not reversible")))
        if meth is not MISSING:
            return call_special(meth, seq)
        if not _iter._has_sequence_protocol(seq):
            error(TypeError, str_join('', ("'", _name(seq), "' object is not reversible")))
        n = operators.length(seq)
        self = alloc(cls, reversed)
        field_set(self, 0, seq)
        field_set(self, 1, n - 1)
        return self

    def __iter__(self):
        return self

    def __next__(self):
        i = field_get(self, 1)
        seq = field_get(self, 0)
        if i >= 0 and seq is not None:
            try:
                item = operators.getitem(seq, i)
            except (IndexError, StopIteration):
                item = MISSING
            if item is not MISSING:
                field_set(self, 1, i - 1)
                return item
        field_set(self, 1, -1)
        field_set(self, 0, None)
        raise StopIteration

    def __length_hint__(self):
        seq = field_get(self, 0)
        if seq is None:
            return 0
        n = operators.length(seq)
        position = field_get(self, 1) + 1
        return 0 if position > n else position


_iter.next_missing_set(map, _map_next)
_iter.next_missing_set(filter, _filter_next)
_iter.next_missing_set(zip, _zip_next)
_iter.next_missing_set(enumerate, _enumerate_next)
