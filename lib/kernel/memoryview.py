"""memoryview (Objects/memoryobject.c) over the primitives of
runtime/prim/memoryview.c, which hold the views, the managed buffer they
share and the item codecs. This file is the protocol around them: indices,
slices, arguments and the error texts."""

from _lucifer import (
    MISSING, error, str_join, kind, size, ns_items, static_type, type_tp_name, type_lookup,
    int_format, address, b_check, kernel_twin,
    mv_new, mv_release, mv_released, mv_attr, mv_len, mv_tobytes, mv_tolist, mv_item,
    mv_set_item, mv_slice, mv_set_slice, mv_cast, mv_toreadonly, mv_eq, mv_hash,
)
from _lucifer import _boot, operators, _format, slice as _slice, iter as _iter, bytes as _b

_nokw = _b._nokw

kernel_type = _boot.kernel_type
getset = _boot.getset
as_ssize = operators.as_ssize
slice_bounds = _slice.slice_bounds

__all__ = ()


def _name(x):
    return type_tp_name(type(x))


def _cat(*parts):
    return str_join('', parts)


def _is_index(x):
    return kind(x, 'int') or type_lookup(type(x), '__index__') is not MISSING


def _noargs(self, name, args):
    if size(args):
        error(TypeError, _cat('memoryview.', name, '() takes no arguments (', int_format(size(args), 10, False), ' given)'))


def _attr(name):
    def get(self):
        return mv_attr(self, name)
    return get


def _lookup(self, key):
    """The item at an index; None means a slice (the view is in the second place)."""
    ndim = mv_attr(self, 'ndim')
    if _is_index(key):
        if ndim == 0:
            error(TypeError, 'invalid indexing of 0-dim memory')
        if ndim > 1:
            error(NotImplementedError, 'multi-dimensional sub-views are not implemented')
        i = as_ssize(key, IndexError)
        n = mv_len(self)
        return i + n if i < 0 else i
    error(TypeError, 'memoryview: invalid slice key')


def _count(self, key):
    ndim = mv_attr(self, 'ndim')
    if ndim == 0:
        error(TypeError, 'invalid indexing of 0-dim memory')
    return slice_bounds(key, mv_len(self))


def _mv_new1(cls, obj):
    """memoryview(obj) (kernel_twin): the view, without the argument parsing."""
    if not kind(cls, 'type'):
        error(TypeError, _cat('memoryview.__new__(X): X is not a type object (', _name(cls), ')'))
    return mv_new(obj)


@kernel_type(static_type('memoryview'))
class memoryview:
    """Create a new memoryview object which references the given object."""

    def __new__(cls, /, *args, **kwargs):
        if not kind(cls, 'type'):
            error(TypeError, _cat('memoryview.__new__(X): X is not a type object (', _name(cls), ')'))
        obj = MISSING
        n = size(args)
        if n > 1:
            error(TypeError, _cat('memoryview() takes at most 1 argument (', int_format(n, 10, False), ' given)'))
        for a in args:
            obj = a
        for key, value in ns_items(kwargs):
            if key != 'object':
                error(TypeError, _cat("memoryview() got an unexpected keyword argument '", key, "'"))
            if obj is not MISSING:
                error(TypeError, "argument for memoryview() given by name ('object') and position (1)")
            obj = value
        if obj is MISSING:
            error(TypeError, "memoryview() missing required argument 'object' (pos 1)")
        return mv_new(obj)

    kernel_twin(__new__, _mv_new1)

    def __len__(self):
        return mv_len(self)

    def __getitem__(self, key):
        mv_attr(self, 'ndim')
        if kind(key, 'slice'):
            start, step, count = _count(self, key)
            return mv_slice(self, start, step, count)
        if key is Ellipsis:
            return mv_new(self)
        if kind(key, 'tuple') and size(key) == 0 and mv_attr(self, 'ndim') == 0:
            return mv_item(self, 0)
        return mv_item(self, _lookup(self, key))

    def __setitem__(self, key, value):
        mv_attr(self, 'ndim')
        if mv_attr(self, 'readonly'):
            error(TypeError, 'cannot modify read-only memory')
        if kind(key, 'slice'):
            start, step, count = _count(self, key)
            mv_set_slice(self, start, step, count, value)
            return
        mv_set_item(self, _lookup(self, key), value)

    def __delitem__(self, key):
        mv_attr(self, 'ndim')
        if mv_attr(self, 'readonly'):
            error(TypeError, 'cannot modify read-only memory')
        error(TypeError, 'cannot delete memory')

    def __iter__(self):
        return _iter.get_iter(mv_tolist(self))

    def __eq__(self, other):
        return mv_eq(self, other)

    def __ne__(self, other):
        r = mv_eq(self, other)
        return r if r is NotImplemented else not r

    def __hash__(self):
        return mv_hash(self)

    def __repr__(self):
        return _cat('<released memory at ' if mv_released(self) else '<memory at ', address(self), '>')

    def __enter__(self):
        mv_attr(self, 'ndim')
        return self

    def __exit__(self, *args):
        mv_release(self)

    def release(self, *args):
        _noargs(self, 'release', args)
        """Release the underlying buffer exposed by the memoryview object."""
        mv_release(self)

    def tobytes(self, order='C'):
        """Return the data in the buffer as a byte string."""
        if order is None:
            order = 'C'
        if not kind(order, 'str'):
            error(TypeError, _cat("tobytes() argument 'order' must be str or None, not ", _name(order)))
        if order != 'C' and order != 'F' and order != 'A':
            error(ValueError, "order must be 'C', 'F' or 'A'")
        return mv_tobytes(self, 67 if order == 'C' else 70 if order == 'F' else 65)

    def tolist(self, *args):
        _noargs(self, 'tolist', args)
        """Return the data in the buffer as a list of elements."""
        return mv_tolist(self)

    def hex(self, *args, **kwargs):
        """Return the data in the buffer as a str of hexadecimal numbers."""
        return _b._hex(mv_tobytes(self, 67), args, kwargs)

    def toreadonly(self):
        """Return a readonly version of the memoryview."""
        return mv_toreadonly(self)

    def cast(self, *args, **kwargs):
        """Cast a memoryview to a new format or shape."""
        format, shape = _b._parse('cast', args, kwargs, ('format', 'shape'), 1)
        if shape is MISSING:
            shape = None
        if shape is not None:
            if not kind(shape, 'list') and not kind(shape, 'tuple'):
                error(TypeError, 'shape must be a list or a tuple')
            shape = (*shape,)
        if not kind(format, 'str'):
            error(TypeError, _cat("cast() argument 'format' must be str, not ", _name(format)))
        return mv_cast(self, format, shape)

    def count(self, *args, **kwargs):
        value = _b._o(self, 'count', args, kwargs)
        """Count the number of occurrences of a value."""
        n = 0
        for x in mv_tolist(self):
            if x == value:
                n += 1
        return n

    def index(self, value, *args):
        """Return the index of the first occurrence of a value."""
        items = mv_tolist(self)
        n = size(items)
        start = as_ssize(args[0], None) if size(args) > 0 else 0
        stop = as_ssize(args[1], None) if size(args) > 1 else n
        if start < 0:
            start += n
            if start < 0:
                start = 0
        if stop < 0:
            stop += n
        i = start
        while i < n and i < stop:
            if items[i] == value:
                return i
            i += 1
        error(ValueError, 'memoryview.index(x): x not found')

    obj = getset(_attr('obj'))
    nbytes = getset(_attr('nbytes'))
    readonly = getset(_attr('readonly'))
    itemsize = getset(_attr('itemsize'))
    format = getset(_attr('format'))
    ndim = getset(_attr('ndim'))
    shape = getset(_attr('shape'))
    strides = getset(_attr('strides'))
    suboffsets = getset(_attr('suboffsets'))
    c_contiguous = getset(_attr('c_contiguous'))
    f_contiguous = getset(_attr('f_contiguous'))
    contiguous = getset(_attr('contiguous'))

