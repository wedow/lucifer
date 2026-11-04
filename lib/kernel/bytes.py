"""bytes and bytearray (Objects/bytesobject.c, bytearrayobject.c,
bytes_methods.c) over the primitives of runtime/prim/bytes.c, which hold the
loops and the storage. Argument checking and the error texts of the C
methods live here. The methods both types share are module functions that
each class body binds; a primitive that mutates a bytearray takes its
critical section, so a method that is one primitive is atomic."""

from _lucifer import (
    MISSING, error, str_join, kind, size, ns_items, static_type, type_tp_name, type_lookup,
    int_format, text_repr, str_replace, new_check, str_encode, instance_dict,
    b_len, b_check, b_bytes, b_from_ints, b_zeros, b_subtype, b_item, b_slice, b_slice_step,
    b_cmp, b_hash, b_concat, b_repeat, b_find, b_count, b_match, b_replace, b_join, b_split,
    b_splitlines, b_strip, b_case, b_is, b_expandtabs, b_translate, b_hex, b_fromhex, b_iter, b_next, b_hint,
    b_sizeof, ba_new, ba_wrap, ba_splice, ba_set_step, ba_del_step, ba_set_item, ba_pop,
    ba_remove, ba_reverse, ba_resize, ba_irepeat, ba_alloc, list_append,
)
from _lucifer import _boot, operators, _format, slice as _slice, iter as _iter, str as _str

kernel_type = _boot.kernel_type
classmethod_ = _boot.classmethod_
call_special = _boot.call_special
index = operators.index
as_ssize = operators.as_ssize
truth = operators.truth
slice_bounds = _slice.slice_bounds
slice_index = _slice.slice_index

__all__ = ()

_SSIZE_MAX = 9223372036854775807
_bytes = static_type('bytes')
_bytearray = static_type('bytearray')


def _name(x):
    return type_tp_name(type(x))


def _cat(*parts):
    return str_join('', parts)


def _decimal(n):
    return int_format(n, 10, False)


def _plural(n):
    return 'argument' if n == 1 else 'arguments'


def _args(name, args, lo, hi):
    """_PyArg_CheckPositional: args padded with MISSING to hi entries."""
    n = size(args)
    if n < lo or n > hi:
        qual = '' if lo == hi else 'at least ' if n < lo else 'at most '
        want = lo if n < lo else hi
        error(TypeError, _cat(name, ' expected ', qual, _decimal(want), ' ', _plural(want),
                              ', got ', _decimal(n)))
    while n < hi:
        args = (*args, MISSING)
        n += 1
    return args


def _ssize(x):
    n = index(x)
    if n > _SSIZE_MAX or n < -_SSIZE_MAX - 1:
        error(OverflowError, 'Python int too large to convert to C ssize_t')
    return n


def _tname(self):
    """The type named in a method's messages: the class of self, or self for a classmethod."""
    if kind(self, 'type'):
        return type_tp_name(self)
    return 'memoryview' if kind(self, 'memoryview') else _kind(self)


def _nokw(self, name, kwargs):
    if kwargs:
        error(TypeError, _cat(_tname(self), '.', name, '() takes no keyword arguments'))


def _a(self, name, args, kwargs, lo, hi):
    """A fastcall method's positional arguments, padded."""
    _nokw(self, name, kwargs)
    return _args(name, args, lo, hi)


def _o(self, name, args, kwargs):
    """The one argument of a METH_O method."""
    _nokw(self, name, kwargs)
    n = size(args)
    if n != 1:
        error(TypeError, _cat(_tname(self), '.', name, '() takes exactly one argument (',
                              _decimal(n), ' given)'))
    for x in args:
        return x


def _parse(name, args, kwargs, names, lo, posonly=0):
    """Positional or keyword arguments (clinic): the values in order of
    names, MISSING where absent."""
    n = size(args)
    top = size(names)
    if n > top:
        error(TypeError, _cat(name, '() takes at most ', _decimal(top), ' ', _plural(top),
                              ' (', _decimal(n), ' given)'))
    values = [*args]
    while size(values) < top:
        list_append(values, MISSING)
    for key, value in ns_items(kwargs):
        i = 0
        found = False
        for nm in names:
            if nm == key:
                found = True
                if values[i] is not MISSING:
                    error(TypeError, _cat('argument for ', name, "() given by name ('", key,
                                          "') and position (", _decimal(i + 1), ')'))
                values[i] = value
            i += 1
        if not found:
            error(TypeError, _cat(name, "() got an unexpected keyword argument '", key, "'"))
    if lo > 0 and values[lo - 1] is MISSING:
        if posonly:
            error(TypeError, _cat(name, '() takes at least ', _decimal(lo), ' positional ', _plural(lo),
                                  ' (', _decimal(n), ' given)'))
        error(TypeError, _cat(name, "() missing required argument '", names[lo - 1], "' (pos ",
                              _decimal(lo), ')'))
    return values


def _noargs(self, name, args):
    n = size(args)
    if n:
        error(TypeError, _cat(_kind(self), '.', name, '() takes no arguments (', _decimal(n), ' given)'))


def _kind(self):
    return 'bytearray' if kind(self, 'bytearray') else 'bytes'


def _out(self, data):
    """A method's result: bytes for bytes, a new bytearray for bytearray."""
    if kind(self, 'bytearray'):
        return ba_wrap(_bytearray, data)
    return data


def _byte(x):
    """_getbytevalue: an int in range(256)."""
    v = index(x)
    if v < 0 or v > 255:
        error(ValueError, 'byte must be in range(0, 256)')
    return v


def _sub(x):
    """The needle of find and friends: bytes-like, or an int taken as one byte."""
    if b_check(x):
        return x
    if kind(x, 'int') or type_lookup(type(x), '__index__') is not MISSING:
        return b_from_ints((_byte(x),))
    error(TypeError, _cat('argument should be integer or bytes-like object, not ', "'", _name(x), "'"))


def _bound(x, default):
    if x is MISSING or x is None:
        return default
    return slice_index(x)


def _find(self, args, name, last):
    sub, start, end = _args(name, args, 1, 3)
    sub = _sub(sub)
    return b_find(self, sub, _bound(start, 0), _bound(end, _SSIZE_MAX), last)


def _find_index(self, args, name, last):
    r = _find(self, args, name, last)
    if r < 0:
        error(ValueError, 'subsection not found')
    return r


def _tailmatch(self, args, name, at_end):
    prefix, start, end = _args(name, args, 1, 3)
    start = _bound(start, 0)
    end = _bound(end, _SSIZE_MAX)
    if kind(prefix, 'tuple'):
        for p in prefix:
            if b_match(self, p, start, end, at_end):
                return True
        return False
    if not b_check(prefix):
        error(TypeError, _cat(name, ' first arg must be bytes or a tuple of bytes, not ', _name(prefix)))
    return b_match(self, prefix, start, end, at_end)


def _split_args(name, args, kwargs):
    sep, maxsplit = _parse(name, args, kwargs, ('sep', 'maxsplit'), 0)
    if sep is MISSING:
        sep = None
    return sep, -1 if maxsplit is MISSING else as_ssize(maxsplit, None)


def _all(self, pieces):
    return [*[_out(self, p) for p in pieces]]


def _fill_arg(name, fill):
    if fill is MISSING:
        return b' '
    if kind(fill, 'bytes') or kind(fill, 'bytearray'):
        if b_len(fill) == 1:
            return b_bytes(fill)
        error(TypeError, _cat(name, '(): argument 2 must be a byte string of length 1, not a ',
                              _name(fill), ' object of length ', _decimal(b_len(fill))))
    error(TypeError, _cat(name, '() argument 2 must be a byte string of length 1, not ', _name(fill)))


def _pad(fill, n):
    return b_repeat(fill, n) if n > 0 else b''


def _concat_error(other, self):
    error(TypeError, _cat("can't concat ", _name(other), ' to ', _name(self)))


def _hex(self, args, kwargs):
    sep, group = _parse('hex', args, kwargs, ('sep', 'bytes_per_sep'), 0)
    byte = -1
    if sep is not MISSING:
        if kind(sep, 'str'):
            n = size_of_str(sep)
        elif b_check(sep):
            n = b_len(sep)
        else:
            n = operators.length(sep)
        if n != 1:
            error(ValueError, 'sep must be length 1.')
        data = str_encode(sep, 'latin-1', 'strict') if kind(sep, 'str') else b_bytes(sep)
        byte = b_item(data, 0)
        if byte > 127:
            error(ValueError, 'sep must be ASCII.')
    return b_hex(self, byte, 1 if group is MISSING else as_ssize(group, None))


def size_of_str(s):
    return operators.length(s)


# shared methods ------------------------------------------------------------

def _m_find(self, *args, **kwargs):
    _nokw(self, 'find', kwargs)
    return _find(self, args, 'find', False)


def _m_rfind(self, *args, **kwargs):
    _nokw(self, 'rfind', kwargs)
    return _find(self, args, 'rfind', True)


def _m_index(self, *args, **kwargs):
    _nokw(self, 'index', kwargs)
    return _find_index(self, args, 'index', False)


def _m_rindex(self, *args, **kwargs):
    _nokw(self, 'rindex', kwargs)
    return _find_index(self, args, 'rindex', True)


def _m_count(self, *args, **kwargs):
    _nokw(self, 'count', kwargs)
    sub, start, end = _args('count', args, 1, 3)
    return b_count(self, _sub(sub), _bound(start, 0), _bound(end, _SSIZE_MAX))


def _m_startswith(self, *args, **kwargs):
    _nokw(self, 'startswith', kwargs)
    return _tailmatch(self, args, 'startswith', False)


def _m_endswith(self, *args, **kwargs):
    _nokw(self, 'endswith', kwargs)
    return _tailmatch(self, args, 'endswith', True)


def _m_split(self, *args, **kwargs):
    sep, maxsplit = _split_args('split', args, kwargs)
    return _all(self, b_split(self, sep, maxsplit, False))


def _m_rsplit(self, *args, **kwargs):
    sep, maxsplit = _split_args('rsplit', args, kwargs)
    return _all(self, b_split(self, sep, maxsplit, True))


def _m_splitlines(self, *args, **kwargs):
    keepends, = _parse('splitlines', args, kwargs, ('keepends',), 0)
    return _all(self, b_splitlines(self, False if keepends is MISSING else True if truth(keepends) else False))


def _partition(self, sep, last):
    if not b_check(sep):
        b_bytes(sep)
    if b_len(sep) == 0:
        error(ValueError, 'empty separator')
    n = b_len(self)
    at = b_find(self, sep, 0, n, last)
    if at < 0:
        empty = _out(self, b'')
        if last:
            return (empty, empty, _out(self, b_bytes(self)))
        return (_out(self, b_bytes(self)), empty, empty)
    return (_out(self, b_slice(self, 0, at)), _out(self, b_bytes(sep)),
            _out(self, b_slice(self, at + b_len(sep), n)))


def _m_partition(self, *args, **kwargs):
    return _partition(self, _o(self, 'partition', args, kwargs), False)


def _m_rpartition(self, *args, **kwargs):
    return _partition(self, _o(self, 'rpartition', args, kwargs), True)


def _m_join(self, *args, **kwargs):
    iterable = _o(self, 'join', args, kwargs)
    if kind(iterable, 'tuple'):
        items = iterable
    elif kind(iterable, 'list'):
        items = (*iterable,)
    else:
        if (type_lookup(type(iterable), '__iter__') is MISSING
                and type_lookup(type(iterable), '__getitem__') is MISSING):
            error(TypeError, 'can only join an iterable')
        items = _iter.tuple_of(iterable)
    return _out(self, b_join(self, items))


def _m_replace(self, *args, **kwargs):
    old, new, count = _a(self, 'replace', args, kwargs, 2, 3)
    return _out(self, b_replace(self, old, new, -1 if count is MISSING else as_ssize(count, None)))


def _chars(args, name):
    chars, = _args(name, args, 0, 1)
    return None if chars is MISSING else chars


def _m_strip(self, *args, **kwargs):
    _nokw(self, 'strip', kwargs)
    return _out(self, b_strip(self, _chars(args, 'strip'), 3))


def _m_lstrip(self, *args, **kwargs):
    _nokw(self, 'lstrip', kwargs)
    return _out(self, b_strip(self, _chars(args, 'lstrip'), 1))


def _m_rstrip(self, *args, **kwargs):
    _nokw(self, 'rstrip', kwargs)
    return _out(self, b_strip(self, _chars(args, 'rstrip'), 2))


def _m_removeprefix(self, *args, **kwargs):
    prefix = _o(self, 'removeprefix', args, kwargs)
    n = b_len(prefix) if b_check(prefix) else b_len(b_bytes(prefix))
    if n and b_match(self, prefix, 0, _SSIZE_MAX, False):
        return _out(self, b_slice(self, n, b_len(self)))
    return _out(self, b_bytes(self))


def _m_removesuffix(self, *args, **kwargs):
    suffix = _o(self, 'removesuffix', args, kwargs)
    n = b_len(suffix) if b_check(suffix) else b_len(b_bytes(suffix))
    if n and b_match(self, suffix, 0, _SSIZE_MAX, True):
        return _out(self, b_slice(self, 0, b_len(self) - n))
    return _out(self, b_bytes(self))


def _case(mode, name):
    def method(self, *args):
        _noargs(self, name, args)
        return _out(self, b_case(self, mode))
    return method


def _is(mode, name):
    def method(self, *args):
        _noargs(self, name, args)
        return b_is(self, mode)
    return method


def _m_center(self, *args, **kwargs):
    width, fill = _a(self, 'center', args, kwargs, 1, 2)
    width = as_ssize(width, None)
    fill = _fill_arg('center', fill)
    n = b_len(self)
    if width <= n:
        return _out(self, b_bytes(self))
    marg = width - n
    left = marg // 2 + (marg & width & 1)
    return _out(self, b_concat(b_concat(_pad(fill, left), self), _pad(fill, marg - left)))


def _m_ljust(self, *args, **kwargs):
    width, fill = _a(self, 'ljust', args, kwargs, 1, 2)
    width = as_ssize(width, None)
    fill = _fill_arg('ljust', fill)
    n = b_len(self)
    return _out(self, b_concat(self, _pad(fill, width - n)))


def _m_rjust(self, *args, **kwargs):
    width, fill = _a(self, 'rjust', args, kwargs, 1, 2)
    width = as_ssize(width, None)
    fill = _fill_arg('rjust', fill)
    n = b_len(self)
    return _out(self, b_concat(_pad(fill, width - n), self))


def _m_zfill(self, *args, **kwargs):
    width = _o(self, 'zfill', args, kwargs)
    width = as_ssize(width, None)
    n = b_len(self)
    if width <= n:
        return _out(self, b_bytes(self))
    fill = width - n
    if n and (b_item(self, 0) == 43 or b_item(self, 0) == 45):
        return _out(self, b_concat(b_concat(b_slice(self, 0, 1), _pad(b'0', fill)), b_slice(self, 1, n)))
    return _out(self, b_concat(_pad(b'0', fill), self))


def _m_expandtabs(self, *args, **kwargs):
    tabsize, = _parse('expandtabs', args, kwargs, ('tabsize',), 0)
    return _out(self, b_expandtabs(self, 8 if tabsize is MISSING else as_ssize(tabsize, None)))


def _m_translate(self, *args, **kwargs):
    table, delete = _parse('translate', args, kwargs, ('table', 'delete'), 1, 1)
    if delete is MISSING:
        delete = b''
    if table is not None:
        if not b_check(table):
            b_bytes(table)
        if b_len(table) != 256:
            error(ValueError, 'translation table must be 256 characters long')
    return _out(self, b_translate(self, table, delete))


def _m_maketrans(cls, *args, **kwargs):
    frm, to = _a(cls, 'maketrans', args, kwargs, 2, 2)
    if b_len(frm) != b_len(to):
        error(ValueError, 'maketrans arguments must have same length')
    f = b_bytes(frm)
    t = b_bytes(to)
    out = []
    i = 0
    while i < 256:
        list_append(out, i)
        i += 1
    i = 0
    while i < b_len(f):
        out[b_item(f, i)] = b_item(t, i)
        i += 1
    return b_from_ints(out)


def _m_decode(self, *args, **kwargs):
    encoding, errors = _parse('decode', args, kwargs, ('encoding', 'errors'), 0)
    if encoding is MISSING:
        encoding = 'utf-8'
    elif not kind(encoding, 'str'):
        error(TypeError, _cat("decode() argument 'encoding' must be str, not ", _name(encoding)))
    if errors is MISSING:
        errors = 'strict'
    elif not kind(errors, 'str'):
        error(TypeError, _cat("decode() argument 'errors' must be str, not ", _name(errors)))
    return _str._decode(b_bytes(self), encoding, errors)


def _m_hex(self, *args, **kwargs):
    return _hex(self, args, kwargs)


def _m_fromhex(cls, *args, **kwargs):
    string = _o(cls, 'fromhex', args, kwargs)
    r = b_fromhex(string)
    if cls is _bytearray:
        return ba_wrap(cls, r)
    if cls is _bytes:
        return r
    return cls(r)


def _m_len(self):
    return b_len(self)


def _m_iter(self):
    return b_iter(self)


def _m_getitem(self, key):
    what = _kind(self)
    if kind(key, 'int') or type_lookup(type(key), '__index__') is not MISSING:
        i = as_ssize(key, IndexError)
        n = b_len(self)
        if i < 0:
            i += n
        if i < 0 or i >= n:
            error(IndexError, 'index out of range' if what == 'bytes' else 'bytearray index out of range')
        return b_item(self, i)
    if kind(key, 'slice'):
        n = b_len(self)
        start, step, count = slice_bounds(key, n)
        if step == 1:
            r = b_slice(self, start, start + count)
        else:
            r = b_slice_step(self, start, step, count)
        return _out(self, r)
    error(TypeError, _cat(what if what == 'bytearray' else 'byte', ' indices must be integers or slices, not ',
                          _name(key)))


def _m_contains(self, item):
    if kind(item, 'int') or type_lookup(type(item), '__index__') is not MISSING:
        item = b_from_ints((_byte(item),))
    return b_find(self, item, 0, _SSIZE_MAX, False) >= 0


def _m_add(self, other):
    if not b_check(other):
        _concat_error(other, self)
    return _out(self, b_concat(self, other))


def _m_mul(self, n):
    if not kind(n, 'int') and type_lookup(type(n), '__index__') is MISSING:
        error(TypeError, _cat("can't multiply sequence by non-int of type '", _name(n), "'"))
    return _out(self, b_repeat(self, as_ssize(n, OverflowError)))


def _compare(self, other, op):
    if not b_check(other):
        return NotImplemented
    c = b_cmp(self, other)
    if op == 0:
        return c < 0
    if op == 1:
        return c <= 0
    if op == 2:
        return c == 0
    if op == 3:
        return c != 0
    if op == 4:
        return c > 0
    return c >= 0


def _m_lt(self, other):
    return _compare(self, other, 0)


def _m_le(self, other):
    return _compare(self, other, 1)


def _m_eq(self, other):
    return _compare(self, other, 2)


def _m_ne(self, other):
    return _compare(self, other, 3)


def _m_gt(self, other):
    return _compare(self, other, 4)


def _m_ge(self, other):
    return _compare(self, other, 5)


def _m_mod(self, args):
    fmt = _str._decode(b_bytes(self), 'latin-1', 'strict')
    r = _format.pct_format(fmt, args, True)
    return _out(self, str_encode(r, 'latin-1', 'strict'))


def _m_rmod(self, other):
    return NotImplemented


def _m_sizeof(self):
    return b_sizeof(self)


def _m_repr(self):
    r = text_repr(b_bytes(self), -1)
    if kind(self, 'bytearray'):
        if r[1] == '"':
            r = _cat('b"', str_replace(r[2:-1], "'", "\\'", -1), '"')
        return _cat(_name(self), '(', r, ')')
    return r


# constructors ----------------------------------------------------------------

def _from_object(x, what):
    """PyBytes_FromObject: the bytes of a bytes-like object or an iterable of ints."""
    if b_check(x):
        return b_bytes(x)
    if kind(x, 'str') or (type_lookup(type(x), '__iter__') is MISSING
                          and type_lookup(type(x), '__getitem__') is MISSING):
        error(TypeError, _cat("cannot convert '", _name(x), "' object to ", what))
    if kind(x, 'list') or kind(x, 'tuple'):
        items = x
    else:
        items = []
        _iter.extend(items, x)
    return b_from_ints([*[index(v) for v in items]])


def _encoded(x, encoding, errors, what):
    """The arguments of bytes(str, encoding, errors), checked."""
    if encoding is not MISSING and not kind(encoding, 'str'):
        error(TypeError, _cat(what, "() argument 'encoding' must be str, not ", _name(encoding)))
    if errors is not MISSING and not kind(errors, 'str'):
        error(TypeError, _cat(what, "() argument 'errors' must be str, not ", _name(errors)))


def _bytes_of(args, kwargs):
    """bytes_new_impl: the exact bytes bytes(source, encoding, errors) makes."""
    x, encoding, errors = _parse('bytes', args, kwargs, ('source', 'encoding', 'errors'), 0)
    _encoded(x, encoding, errors, 'bytes')
    if x is MISSING:
        if encoding is not MISSING or errors is not MISSING:
            error(TypeError, 'encoding without a string argument' if encoding is not MISSING
                  else 'errors without a string argument')
        return b''
    if encoding is not MISSING:
        if not kind(x, 'str'):
            error(TypeError, 'encoding without a string argument')
        return _str._encode(x, encoding, 'strict' if errors is MISSING else errors)
    if errors is not MISSING:
        error(TypeError, 'string argument without an encoding' if kind(x, 'str')
              else 'errors without a string argument')
    f = type_lookup(type(x), '__bytes__')
    if f is not MISSING:
        r = call_special(f, x)
        if not kind(r, 'bytes'):
            error(TypeError, _cat('__bytes__ returned non-bytes (type ', _name(r), ')'))
        return b_bytes(r)
    if kind(x, 'str'):
        error(TypeError, 'string argument without an encoding')
    if kind(x, 'int') or type_lookup(type(x), '__index__') is not MISSING:
        n = as_ssize(x, OverflowError)
        if n < 0:
            error(ValueError, 'negative count')
        return b_zeros(n)
    return _from_object(x, 'bytes')


def _all_ints(x):
    for v in x:
        if type(v) is not int:
            return False
    return True


def _bytearray_fill(self, args, kwargs):
    """bytearray_init: replaces self's contents by the source."""
    x, encoding, errors = _parse('bytearray', args, kwargs, ('source', 'encoding', 'errors'), 0)
    _encoded(x, encoding, errors, 'bytearray')
    n = b_len(self)
    ba_splice(self, 0, n, b'')
    n = 0
    if x is MISSING:
        if encoding is not MISSING or errors is not MISSING:
            error(TypeError, 'encoding without a string argument' if encoding is not MISSING
                  else 'errors without a string argument')
        return
    if kind(x, 'str'):
        if encoding is MISSING:
            error(TypeError, 'string argument without an encoding')
        data = _str._encode(x, encoding, 'strict' if errors is MISSING else errors)
    elif encoding is not MISSING or errors is not MISSING:
        error(TypeError, 'encoding without a string argument' if encoding is not MISSING
              else 'errors without a string argument')
    elif kind(x, 'int') or type_lookup(type(x), '__index__') is not MISSING:
        count = as_ssize(x, OverflowError)
        if count < 0:
            error(ValueError, 'negative count')
        data = b_zeros(count)
    elif b_check(x):
        data = b_bytes(x)
    elif (type(x) is list or type(x) is tuple) and _all_ints(x):
        data = b_from_ints([*[_byte(v) for v in x]])
    else:
        if (type_lookup(type(x), '__iter__') is MISSING
                and type_lookup(type(x), '__getitem__') is MISSING):
            error(TypeError, _cat("cannot convert '", _name(x), "' object to bytearray"))
        it = _iter.get_iter(x)
        while True:
            v = _iter.next_item(it)
            if v is MISSING:
                return
            ba_splice(self, _SSIZE_MAX, _SSIZE_MAX, b_from_ints((_byte(v),)))
    ba_splice(self, 0, n, data)


def _extend(self, iterable):
    """bytearray.extend: a buffer is spliced in; anything else is read in full first."""
    if b_check(iterable):
        ba_splice(self, _SSIZE_MAX, _SSIZE_MAX, iterable)
        return
    if (type_lookup(type(iterable), '__iter__') is MISSING
            and type_lookup(type(iterable), '__getitem__') is MISSING):
        error(TypeError, _cat("can't extend bytearray with ", _name(iterable)))
    items = []
    it = _iter.get_iter(iterable)
    while True:
        v = _iter.next_item(it)
        if v is MISSING:
            break
        try:
            list_append(items, _byte(v))
        except TypeError:
            if kind(iterable, 'str'):
                error(TypeError, "expected iterable of integers; got: 'str'")
            raise
    ba_splice(self, _SSIZE_MAX, _SSIZE_MAX, b_from_ints(items))


@kernel_type(_bytes)
class bytes:
    """bytes(iterable_of_ints) -> bytes
bytes(string, encoding[, errors]) -> bytes
bytes(bytes_or_buffer) -> immutable copy of bytes_or_buffer
bytes(int) -> bytes object of size given by the parameter initialized with null bytes
bytes() -> empty bytes object

Construct an immutable array of bytes from:
  - an iterable yielding integers in range(256)
  - a text string encoded using the specified encoding
  - any object implementing the buffer API.
  - an integer"""

    def __new__(cls, *args, **kwargs):
        if not kind(cls, 'type'):
            error(TypeError, _cat('bytes.__new__(X): X is not a type object (', _name(cls), ')'))
        if cls is not bytes:
            new_check(cls, bytes)
        value = _bytes_of(args, kwargs)
        if cls is bytes:
            return value
        return b_subtype(cls, value)

    def __repr__(self):
        return _m_repr(self)

    def __str__(self):
        return _m_repr(self)

    def __hash__(self):
        return b_hash(self)

    def __bytes__(self):
        return b_bytes(self)

    def __getnewargs__(self):
        return (b_bytes(self),)

    __lt__ = _m_lt
    __le__ = _m_le
    __eq__ = _m_eq
    __ne__ = _m_ne
    __gt__ = _m_gt
    __ge__ = _m_ge
    __len__ = _m_len
    __iter__ = _m_iter
    __getitem__ = _m_getitem
    __contains__ = _m_contains
    __add__ = _m_add
    __mul__ = _m_mul
    __rmul__ = _m_mul
    __mod__ = _m_mod
    __rmod__ = _m_rmod
    __sizeof__ = _m_sizeof
    find = _m_find
    rfind = _m_rfind
    index = _m_index
    rindex = _m_rindex
    count = _m_count
    startswith = _m_startswith
    endswith = _m_endswith
    split = _m_split
    rsplit = _m_rsplit
    splitlines = _m_splitlines
    partition = _m_partition
    rpartition = _m_rpartition
    join = _m_join
    replace = _m_replace
    strip = _m_strip
    lstrip = _m_lstrip
    rstrip = _m_rstrip
    removeprefix = _m_removeprefix
    removesuffix = _m_removesuffix
    lower = _case(0, 'lower')
    upper = _case(1, 'upper')
    swapcase = _case(2, 'swapcase')
    title = _case(3, 'title')
    capitalize = _case(4, 'capitalize')
    isalpha = _is(0, 'isalpha')
    isalnum = _is(1, 'isalnum')
    isdigit = _is(2, 'isdigit')
    isspace = _is(3, 'isspace')
    islower = _is(4, 'islower')
    isupper = _is(5, 'isupper')
    istitle = _is(6, 'istitle')
    isascii = _is(7, 'isascii')
    center = _m_center
    ljust = _m_ljust
    rjust = _m_rjust
    zfill = _m_zfill
    expandtabs = _m_expandtabs
    translate = _m_translate
    maketrans = classmethod_(_m_maketrans)
    decode = _m_decode
    hex = _m_hex
    fromhex = classmethod_(_m_fromhex)


def _m_ba_new(cls, *args, **kwargs):
    if not kind(cls, 'type'):
        error(TypeError, _cat('bytearray.__new__(X): X is not a type object (', _name(cls), ')'))
    if cls is not bytearray:
        new_check(cls, bytearray)
    return ba_new(cls)


def _m_ba_init(self, *args, **kwargs):
    _bytearray_fill(self, args, kwargs)


def _m_ba_setitem(self, key, value):
    if kind(key, 'int') or type_lookup(type(key), '__index__') is not MISSING:
        i = as_ssize(key, IndexError)
        n = b_len(self)
        if i < 0:
            i += n
        if i < 0 or i >= n:
            error(IndexError, 'bytearray index out of range')
        ba_set_item(self, i, _byte(value))
        return
    if not kind(key, 'slice'):
        error(TypeError, _cat('bytearray indices must be integers or slices, not ', _name(key)))
    if value is None:
        _m_ba_delitem(self, key)
        return
    if b_check(value):
        data = b_bytes(value)
    elif kind(value, 'int') or kind(value, 'str') or (
            type_lookup(type(value), '__iter__') is MISSING
            and type_lookup(type(value), '__getitem__') is MISSING):
        error(TypeError, 'can assign only bytes, buffers, or iterables of ints in range(0, 256)')
    else:
        items = []
        _iter.extend(items, value)
        data = b_from_ints([*[_byte(v) for v in items]])
    start, step, count = slice_bounds(key, b_len(self))
    if step == 1:
        ba_splice(self, start, start + count, data)
        return
    if b_len(data) != count:
        error(ValueError, _cat('attempt to assign bytes of size ', _decimal(b_len(data)),
                               ' to extended slice of size ', _decimal(count)))
    ba_set_step(self, start, step, count, data)


def _m_ba_delitem(self, key):
    if kind(key, 'int') or type_lookup(type(key), '__index__') is not MISSING:
        i = as_ssize(key, IndexError)
        n = b_len(self)
        if i < 0:
            i += n
        if i < 0 or i >= n:
            error(IndexError, 'bytearray index out of range')
        ba_splice(self, i, i + 1, b'')
        return
    if not kind(key, 'slice'):
        error(TypeError, _cat('bytearray indices must be integers or slices, not ', _name(key)))
    start, step, count = slice_bounds(key, b_len(self))
    if step == 1:
        ba_splice(self, start, start + count, b'')
    else:
        ba_del_step(self, start, step, count)


def _m_ba_iadd(self, other):
    if not b_check(other):
        _concat_error(other, self)
    ba_splice(self, _SSIZE_MAX, _SSIZE_MAX, other)
    return self


def _m_ba_imul(self, n):
    if not kind(n, 'int') and type_lookup(type(n), '__index__') is MISSING:
        error(TypeError, _cat("can't multiply sequence by non-int of type '", _name(n), "'"))
    ba_irepeat(self, as_ssize(n, OverflowError))
    return self


def _m_ba_reduce_ex(self, protocol=0):
    state = instance_dict(self, False)
    if index(protocol) > 2:
        return (type(self), (b_bytes(self),), state)
    return (type(self), (_str._decode(b_bytes(self), 'latin-1', 'strict'), 'latin-1'), state)


def _m_ba_reduce(self):
    return _m_ba_reduce_ex(self, 0)


@kernel_type(_bytearray)
class bytearray:
    """bytearray(iterable_of_ints) -> bytearray
bytearray(string, encoding[, errors]) -> bytearray
bytearray(bytes_or_buffer) -> mutable copy of bytes_or_buffer
bytearray(int) -> bytes array of size given by the parameter initialized with null bytes
bytearray() -> empty bytes array

Construct a mutable bytearray object from:
  - an iterable yielding integers in range(256)
  - a text string encoded using the specified encoding
  - a bytes or a buffer object
  - any object implementing the buffer API.
  - an integer"""

    __new__ = _m_ba_new
    __init__ = _m_ba_init
    __hash__ = None
    __repr__ = _m_repr
    __str__ = _m_repr
    __len__ = _m_len
    __iter__ = _m_iter
    __getitem__ = _m_getitem
    __setitem__ = _m_ba_setitem
    __delitem__ = _m_ba_delitem
    __contains__ = _m_contains
    __add__ = _m_add
    __iadd__ = _m_ba_iadd
    __mul__ = _m_mul
    __rmul__ = _m_mul
    __imul__ = _m_ba_imul
    __mod__ = _m_mod
    __rmod__ = _m_rmod
    __sizeof__ = _m_sizeof
    __reduce__ = _m_ba_reduce
    __reduce_ex__ = _m_ba_reduce_ex
    find = _m_find
    rfind = _m_rfind
    index = _m_index
    rindex = _m_rindex
    count = _m_count
    startswith = _m_startswith
    endswith = _m_endswith
    split = _m_split
    rsplit = _m_rsplit
    splitlines = _m_splitlines
    partition = _m_partition
    rpartition = _m_rpartition
    join = _m_join
    replace = _m_replace
    strip = _m_strip
    lstrip = _m_lstrip
    rstrip = _m_rstrip
    removeprefix = _m_removeprefix
    removesuffix = _m_removesuffix
    lower = _case(0, 'lower')
    upper = _case(1, 'upper')
    swapcase = _case(2, 'swapcase')
    title = _case(3, 'title')
    capitalize = _case(4, 'capitalize')
    isalpha = _is(0, 'isalpha')
    isalnum = _is(1, 'isalnum')
    isdigit = _is(2, 'isdigit')
    isspace = _is(3, 'isspace')
    islower = _is(4, 'islower')
    isupper = _is(5, 'isupper')
    istitle = _is(6, 'istitle')
    isascii = _is(7, 'isascii')
    center = _m_center
    ljust = _m_ljust
    rjust = _m_rjust
    zfill = _m_zfill
    expandtabs = _m_expandtabs
    translate = _m_translate
    maketrans = classmethod_(_m_maketrans)
    decode = _m_decode
    hex = _m_hex
    fromhex = classmethod_(_m_fromhex)

    __lt__ = _m_lt
    __le__ = _m_le
    __eq__ = _m_eq
    __ne__ = _m_ne
    __gt__ = _m_gt
    __ge__ = _m_ge
    def __alloc__(self):
        return ba_alloc(self)

    def append(self, *args, **kwargs):
        """Append a single item to the end of the bytearray."""
        item = _o(self, 'append', args, kwargs)
        ba_splice(self, _SSIZE_MAX, _SSIZE_MAX, b_from_ints((_byte(item),)))

    def clear(self, *args):
        _noargs(self, 'clear', args)
        """Remove all items from the bytearray."""
        ba_splice(self, 0, _SSIZE_MAX, b'')

    def copy(self, *args):
        _noargs(self, 'copy', args)
        """Return a copy of B."""
        return ba_wrap(bytearray, self)

    def extend(self, *args, **kwargs):
        """Append all the items from the iterator or sequence to the end of the bytearray."""
        _extend(self, _o(self, 'extend', args, kwargs))

    def insert(self, *args, **kwargs):
        """Insert a single item into the bytearray before the given index."""
        index, item = _a(self, 'insert', args, kwargs, 2, 2)
        i = as_ssize(index, None)
        v = _byte(item)
        n = b_len(self)
        if i < 0:
            i += n
            if i < 0:
                i = 0
        if i > n:
            i = n
        ba_splice(self, i, i, b_from_ints((v,)))

    def pop(self, *args, **kwargs):
        """Remove and return a single item from B."""
        index, = _a(self, 'pop', args, kwargs, 0, 1)
        return ba_pop(self, -1 if index is MISSING else as_ssize(index, None))

    def remove(self, *args, **kwargs):
        """Remove the first occurrence of a value in the bytearray."""
        ba_remove(self, _byte(_o(self, 'remove', args, kwargs)))

    def reverse(self, *args):
        _noargs(self, 'reverse', args)
        """Reverse the order of the values in B in place."""
        ba_reverse(self)

    def resize(self, *args, **kwargs):
        """Resize the internal buffer of bytearray to len."""
        ba_resize(self, _ssize(_o(self, 'resize', args, kwargs)))


operators.add_sequence(bytes)
operators.add_sequence(bytearray)


@kernel_type(static_type('bytes_iterator'))
class bytes_iterator:
    def __iter__(self):
        return self

    def __next__(self):
        v = b_next(self)
        if v is MISSING:
            raise StopIteration
        return v

    def __length_hint__(self):
        return b_hint(self)

    def __reduce__(self):
        return _iter.seqiter_reduce(self, ())

    def __setstate__(self, state):
        _iter.seqiter_setstate(self, state)


@kernel_type(static_type('bytearray_iterator'))
class bytearray_iterator:
    def __iter__(self):
        return self

    def __next__(self):
        v = b_next(self)
        if v is MISSING:
            raise StopIteration
        return v

    def __length_hint__(self):
        return b_hint(self)

    def __reduce__(self):
        return _iter.seqiter_reduce(self, ())

    def __setstate__(self, state):
        _iter.seqiter_setstate(self, state)
