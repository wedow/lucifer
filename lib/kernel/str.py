"""str (Objects/unicodeobject.c): the methods and dunders of Python's text
over the primitives of runtime/prim/str.c, which hold the loops, the Unicode
database and the codecs. Argument checking and the error texts of the C
methods live here; formatting is _format.py."""

from _lucifer import (
    MISSING, error, str_join, kind, size, ns_items, static_type, type_tp_name, type_lookup,
    is_subtype, int_format, text_repr, new_check, str_find, str_count, str_replace, str_slice,
    str_slice_step, str_item, str_ord, str_chr, str_len, str_hash, str_cmp, str_concat,
    str_repeat, str_split, str_splitlines, str_strip, str_case, str_is, str_expandtabs, str_ascii,
    str_encode, str_decode, str_iter, str_iter_next, str_subtype, str_sizeof, list_append, slice_parts, kernel_set, b_check, b_bytes,
)
from _lucifer import _boot, operators, _format

kernel_type = _boot.kernel_type
classmethod_ = _boot.classmethod_
call_special = _boot.call_special
index = operators.index
to_str = _format.to_str
to_repr = _format.to_repr
to_ascii = _format.to_ascii
format_value = _format.format_value

__all__ = ('str', 'repr', 'ascii', 'format')

_SSIZE_MAX = 9223372036854775807
_SLICE = static_type('slice')
_NONE = type(None)


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
    if n < lo:
        error(TypeError, _cat(name, ' expected at least ', _decimal(lo), ' ', _plural(lo),
                              ', got ', _decimal(n)))
    if n > hi:
        error(TypeError, _cat(name, ' expected at most ', _decimal(hi), ' ', _plural(hi),
                              ', got ', _decimal(n)))
    while n < hi:
        args = (*args, MISSING)
        n += 1
    return args


def _noargs(name, args, kwargs):
    if kwargs:
        error(TypeError, _cat('str.', name, '() takes no keyword arguments'))
    n = size(args)
    if n:
        error(TypeError, _cat('str.', name, '() takes no arguments (', _decimal(n), ' given)'))


def _one(name, args, kwargs):
    """A method taking exactly one positional argument."""
    if kwargs:
        error(TypeError, _cat('str.', name, '() takes no keyword arguments'))
    n = size(args)
    if n != 1:
        error(TypeError, _cat('str.', name, '() takes exactly one argument (', _decimal(n), ' given)'))


def _str_arg(x, fname, pos):
    if not kind(x, 'str'):
        error(TypeError, _cat(fname, '() argument ', pos, ' must be str, not ', _name(x)))
    return x


def _ssize(x):
    """An argument taken as Py_ssize_t, through __index__."""
    if kind(x, 'int'):
        return x
    return index(x)


def _slice_index(x, default):
    """PyEval_SliceIndex of a start or end argument; None and MISSING are the default."""
    if x is MISSING or x is None:
        return default
    if kind(x, 'int'):
        return x
    if type_lookup(type(x), '__index__') is MISSING:
        error(TypeError, 'slice indices must be integers or None or have an __index__ method')
    return index(x)


def _exact(s):
    return s if type(s) is str else str_slice(s, 0, str_len(s))


def _adjust(n, start, end):
    """ADJUST_INDICES."""
    if end > n:
        end = n
    elif end < 0:
        end += n
        if end < 0:
            end = 0
    if start < 0:
        start += n
        if start < 0:
            start = 0
    return start, end


def _find(self, args, name, last):
    sub, start, end = _args(name, args, 1, 3)
    _str_arg(sub, name, '1')
    start = _slice_index(start, 0)
    end = _slice_index(end, _SSIZE_MAX)
    return str_find(self, sub, start, end, last)


def _tailmatch(self, args, name, at_end):
    prefix, start, end = _args(name, args, 1, 3)
    start = _slice_index(start, 0)
    end = _slice_index(end, _SSIZE_MAX)
    n = str_len(self)
    start, end = _adjust(n, start, end)
    if kind(prefix, 'tuple'):
        for p in prefix:
            if not kind(p, 'str'):
                error(TypeError, _cat('tuple for ', name, ' must only contain str, not ', _name(p)))
            if _match(self, p, start, end, at_end):
                return True
        return False
    if not kind(prefix, 'str'):
        error(TypeError, _cat(name, ' first arg must be str or a tuple of str, not ', _name(prefix)))
    return _match(self, prefix, start, end, at_end)


def _match(s, sub, start, end, at_end):
    m = str_len(sub)
    end -= m
    if end < start:
        return False
    if m == 0:
        return True
    pos = end if at_end else start
    return str_cmp(str_slice(s, pos, pos + m), sub) == 0


def _fill(args, name):
    width, fill = _args(name, args, 1, 2)
    if fill is MISSING:
        fill = ' '
    elif not kind(fill, 'str'):
        error(TypeError, _cat('The fill character must be a unicode character, not ', _name(fill)))
    elif str_len(fill) != 1:
        error(TypeError, 'The fill character must be exactly one character long')
    return _ssize(width), fill


def _pad(fill, n):
    return str_repeat(fill, n) if n > 0 else ''


def _split_args(name, args, kwargs, first):
    """sep and maxsplit from positional or keyword arguments (clinic)."""
    sep = MISSING
    maxsplit = MISSING
    n = size(args)
    if n > 2:
        error(TypeError, _cat(name, '() takes at most 2 arguments (', _decimal(n), ' given)'))
    i = 0
    for a in args:
        if i == 0:
            sep = a
        else:
            maxsplit = a
        i += 1
    for key, value in ns_items(kwargs):
        if key == first and sep is MISSING:
            sep = value
        elif key == 'maxsplit' and maxsplit is MISSING:
            maxsplit = value
        elif key == first or key == 'maxsplit':
            error(TypeError, _cat(name, "() argument '", key, "' given by name ('", key,
                                  "') and position"))
        else:
            error(TypeError, _cat(name, "() got an unexpected keyword argument '", key, "'"))
    if sep is MISSING:
        sep = None
    elif sep is not None and not kind(sep, 'str'):
        error(TypeError, _cat('must be str or None, not ', _name(sep)))
    maxsplit = -1 if maxsplit is MISSING else _ssize(maxsplit)
    return sep, maxsplit


def _in(x, names):
    for n in names:
        if n == x:
            return True
    return False


def _encoding_name(encoding):
    """The primitive codec's canonical name, as codecs.lookup normalizes it, or
    None for an encoding that only the codec registry knows."""
    name = str_replace(str_case(encoding, 0), '-', '_', -1)
    name = str_replace(name, ' ', '_', -1)
    if _in(name, ('utf_8', 'utf8', 'u8', 'utf', 'cp65001', 'utf8_ucs2', 'utf8_ucs4')):
        return 'utf-8'
    if _in(name, ('latin_1', 'latin1', 'iso_8859_1', 'iso8859_1', '8859', 'cp819', 'latin', 'l1',
                'iso_ir_100', 'iso8859_1', 'iso_8859_1_1987')):
        return 'latin-1'
    if _in(name, ('ascii', 'us_ascii', '646', 'ansi_x3.4_1968', 'ansi_x3_4_1968', 'cp367', 'iso646_us',
                'iso_646.irv_1991', 'iso_ir_6', 'us')):
        return 'ascii'
    return None


_HANDLERS = ('strict', 'ignore', 'replace', 'backslashreplace', 'xmlcharrefreplace',
             'surrogateescape', 'surrogatepass')


def _encode(self, encoding, errors):
    name = _encoding_name(encoding)
    if name is None or not _in(errors, _HANDLERS):
        return __import__('_codecs')._encode_text(self, encoding, errors)
    r = str_encode(self, name, errors)
    if kind(r, 'tuple'):
        start, end, reason = r
        raise UnicodeEncodeError(name, self, start, end, reason)
    return r


def _decode(data, encoding, errors):
    name = _encoding_name(encoding)
    if name is None or not _in(errors, _HANDLERS):
        return __import__('_codecs')._decode_text(data, encoding, errors)
    r = str_decode(data, name, errors)
    if kind(r, 'tuple'):
        start, end, reason = r
        raise UnicodeDecodeError(name, data, start, end, reason)
    return r


def _str_new(args, kwargs):
    """unicode_new_impl: str(object='', encoding=..., errors=...)."""
    obj = MISSING
    encoding = MISSING
    errors = MISSING
    n = size(args)
    if n > 3:
        error(TypeError, _cat('str expected at most 3 arguments, got ', _decimal(n)))
    i = 0
    for a in args:
        if i == 0:
            obj = a
        elif i == 1:
            encoding = a
        else:
            errors = a
        i += 1
    for key, value in ns_items(kwargs):
        if key == 'object' and obj is MISSING:
            obj = value
        elif key == 'encoding' and encoding is MISSING:
            encoding = value
        elif key == 'errors' and errors is MISSING:
            errors = value
        elif key == 'object' or key == 'encoding' or key == 'errors':
            error(TypeError, _cat("argument for str() given by name ('", key, "') and position"))
        else:
            error(TypeError, _cat("str() got an unexpected keyword argument '", key, "'"))
    if encoding is not MISSING and not kind(encoding, 'str'):
        error(TypeError, _cat("str() argument 'encoding' must be str, not ", _name(encoding)))
    if errors is not MISSING and not kind(errors, 'str'):
        error(TypeError, _cat("str() argument 'errors' must be str, not ", _name(errors)))
    if obj is MISSING:
        return ''
    if encoding is MISSING and errors is MISSING:
        return to_str(obj)
    if kind(obj, 'str'):
        error(TypeError, 'decoding str is not supported')
    if not b_check(obj):
        error(TypeError, _cat('decoding to str: need a bytes-like object, ', _name(obj), ' found'))
    return _decode(b_bytes(obj), 'utf-8' if encoding is MISSING else encoding,
                   'strict' if errors is MISSING else errors)


def _unpack_slice(sl, length):
    """PySlice_Unpack and PySlice_AdjustIndices: (start, step, count)."""
    start, stop, step = slice_parts(sl)
    step = 1 if step is None else _slice_int(step)
    if step == 0:
        error(ValueError, 'slice step cannot be zero')
    if step < -_SSIZE_MAX:
        step = -_SSIZE_MAX
    if start is None:
        start = _SSIZE_MAX if step < 0 else 0
    else:
        start = _slice_int(start)
    if stop is None:
        stop = -_SSIZE_MAX - 1 if step < 0 else _SSIZE_MAX
    else:
        stop = _slice_int(stop)
    if start < 0:
        start += length
        if start < 0:
            start = -1 if step < 0 else 0
    elif start >= length:
        start = length - 1 if step < 0 else length
    if stop < 0:
        stop += length
        if stop < 0:
            stop = -1 if step < 0 else 0
    elif stop >= length:
        stop = length - 1 if step < 0 else length
    if step < 0:
        count = (start - stop - 1) // (-step) + 1 if stop < start else 0
    else:
        count = (stop - start - 1) // step + 1 if start < stop else 0
    return start, step, count


def _slice_int(x):
    if kind(x, 'int'):
        return x
    if type_lookup(type(x), '__index__') is MISSING:
        error(TypeError, 'slice indices must be integers or None or have an __index__ method')
    return index(x)


@kernel_type(static_type('str'))
class str:
    """str(object='') -> str
str(bytes_or_buffer[, encoding[, errors]]) -> str

Create a new string object from the given object. If encoding or
errors is specified, then the object must expose a data buffer
that will be decoded using the given encoding and error handler.
Otherwise, returns the result of object.__str__() (if defined)
or repr(object).
encoding defaults to 'utf-8'.
errors defaults to 'strict'."""

    def __new__(cls, *args, **kwargs):
        if not kind(cls, 'type'):
            error(TypeError, _cat('str.__new__(X): X is not a type object (', _name(cls), ')'))
        if cls is not str:
            new_check(cls, str)
        value = _str_new(args, kwargs)
        if cls is str:
            return value
        return str_subtype(cls, value)

    def __repr__(self):
        return text_repr(self, -1)

    def __str__(self):
        return _exact(self)

    def __hash__(self):
        return str_hash(self)

    def __len__(self):
        return str_len(self)

    def __iter__(self):
        return str_iter(self)

    def __getnewargs__(self):
        return (_exact(self),)

    def __sizeof__(self):
        return str_sizeof(self)

    def __rmod__(self, other):
        return NotImplemented

    def __format__(self, format_spec, /):
        if not kind(format_spec, 'str'):
            error(TypeError, _cat('__format__() argument must be str, not ', _name(format_spec)))
        return _format.format_str(self, format_spec)

    def __lt__(self, other):
        if not kind(other, 'str'):
            return NotImplemented
        return str_cmp(self, other) < 0

    def __le__(self, other):
        if not kind(other, 'str'):
            return NotImplemented
        return str_cmp(self, other) <= 0

    def __eq__(self, other):
        if not kind(other, 'str'):
            return NotImplemented
        return str_cmp(self, other) == 0

    def __ne__(self, other):
        if not kind(other, 'str'):
            return NotImplemented
        return str_cmp(self, other) != 0

    def __gt__(self, other):
        if not kind(other, 'str'):
            return NotImplemented
        return str_cmp(self, other) > 0

    def __ge__(self, other):
        if not kind(other, 'str'):
            return NotImplemented
        return str_cmp(self, other) >= 0

    def __contains__(self, item):
        if not kind(item, 'str'):
            error(TypeError, _cat("'in <string>' requires string as left operand, not ", _name(item)))
        return str_find(self, item, 0, _SSIZE_MAX, False) >= 0

    def __add__(self, other):
        if not kind(other, 'str'):
            error(TypeError, _cat('can only concatenate str (not "', _name(other), '") to str'))
        return str_concat(self, other)

    def __mul__(self, n):
        if not kind(n, 'int') and type_lookup(type(n), '__index__') is MISSING:
            error(TypeError, _cat("can't multiply sequence by non-int of type '", _name(n), "'"))
        return str_repeat(self, index(n))

    def __rmul__(self, n):
        if not kind(n, 'int') and type_lookup(type(n), '__index__') is MISSING:
            error(TypeError, _cat("can't multiply sequence by non-int of type '", _name(n), "'"))
        return str_repeat(self, index(n))

    def __mod__(self, other):
        return _format.pct_format(self, other)

    def __getitem__(self, key):
        if is_subtype(type(key), _SLICE):
            start, step, count = _unpack_slice(key, str_len(self))
            if step == 1:
                return str_slice(self, start, start + count)
            return str_slice_step(self, start, step, count)
        if not kind(key, 'int'):
            if type_lookup(type(key), '__index__') is MISSING:
                error(TypeError, _cat('string indices must be integers, not ', "'", _name(key), "'"))
            key = index(key)
        n = str_len(self)
        i = key + n if key < 0 else key
        if i < 0 or i >= n:
            error(IndexError, 'string index out of range')
        return str_item(self, i)

    # searching

    def find(self, *args):
        return _find(self, args, 'find', False)

    def rfind(self, *args):
        return _find(self, args, 'rfind', True)

    def index(self, *args):
        r = _find(self, args, 'index', False)
        if r < 0:
            error(ValueError, 'substring not found')
        return r

    def rindex(self, *args):
        r = _find(self, args, 'rindex', True)
        if r < 0:
            error(ValueError, 'substring not found')
        return r

    def count(self, *args):
        sub, start, end = _args('count', args, 1, 3)
        _str_arg(sub, 'count', '1')
        return str_count(self, sub, _slice_index(start, 0), _slice_index(end, _SSIZE_MAX))

    def startswith(self, *args):
        return _tailmatch(self, args, 'startswith', False)

    def endswith(self, *args):
        return _tailmatch(self, args, 'endswith', True)

    # splitting and joining

    def split(self, *args, **kwargs):
        sep, maxsplit = _split_args('split', args, kwargs, 'sep')
        return str_split(self, sep, maxsplit, False)

    def rsplit(self, *args, **kwargs):
        sep, maxsplit = _split_args('rsplit', args, kwargs, 'sep')
        return str_split(self, sep, maxsplit, True)

    def splitlines(self, *args, **kwargs):
        keepends = False
        n = size(args)
        if n > 1:
            error(TypeError, _cat('splitlines() takes at most 1 argument (', _decimal(n), ' given)'))
        for a in args:
            keepends = a
        for key, value in ns_items(kwargs):
            if key != 'keepends':
                error(TypeError, _cat("splitlines() got an unexpected keyword argument '", key, "'"))
            if n:
                error(TypeError, "argument for splitlines() given by name ('keepends') and position")
            keepends = value
        return str_splitlines(self, True if index(keepends) else False)

    def partition(self, *args, **kwargs):
        _one('partition', args, kwargs)
        for sep in args:
            return _partition(self, sep, False)

    def rpartition(self, *args, **kwargs):
        _one('rpartition', args, kwargs)
        for sep in args:
            return _partition(self, sep, True)

    def join(self, iterable, /):
        if kind(iterable, 'tuple'):
            items = iterable
        elif kind(iterable, 'list'):
            items = (*iterable,)
        else:
            if (type_lookup(type(iterable), '__iter__') is MISSING
                    and type_lookup(type(iterable), '__getitem__') is MISSING):
                error(TypeError, 'can only join an iterable')
            items = (*[x for x in iterable],)
        return str_join(self, items)

    # transformations

    def replace(self, *args, **kwargs):
        n = size(args)
        count = MISSING
        for key, value in ns_items(kwargs):
            if key != 'count':
                error(TypeError, _cat("replace() got an unexpected keyword argument '", key, "'"))
            count = value
        if n < 2:
            error(TypeError, _cat('replace() takes at least 2 positional arguments (', _decimal(n), ' given)'))
        if n > 3 or (n == 3 and count is not MISSING):
            error(TypeError, _cat('replace() takes at most 3 arguments (', _decimal(n), ' given)'))
        if n == 3:
            old, new, count = args
        else:
            old, new = args
        _str_arg(old, 'replace', '1')
        _str_arg(new, 'replace', '2')
        return str_replace(self, old, new, -1 if count is MISSING else _ssize(count))

    def strip(self, *args):
        return str_strip(self, _chars('strip', args), 3)

    def lstrip(self, *args):
        return str_strip(self, _chars('lstrip', args), 1)

    def rstrip(self, *args):
        return str_strip(self, _chars('rstrip', args), 2)

    def removeprefix(self, prefix, /):
        _removable(prefix, 'removeprefix')
        n = str_len(prefix)
        if n and _match(self, prefix, 0, str_len(self), False):
            return str_slice(self, n, str_len(self))
        return _exact(self)

    def removesuffix(self, suffix, /):
        _removable(suffix, 'removesuffix')
        n = str_len(suffix)
        if n and _match(self, suffix, 0, str_len(self), True):
            return str_slice(self, 0, str_len(self) - n)
        return _exact(self)

    def lower(self, *args, **kwargs):
        _noargs('lower', args, kwargs)
        return str_case(self, 0)

    def upper(self, *args, **kwargs):
        _noargs('upper', args, kwargs)
        return str_case(self, 1)

    def casefold(self, *args, **kwargs):
        _noargs('casefold', args, kwargs)
        return str_case(self, 2)

    def title(self, *args, **kwargs):
        _noargs('title', args, kwargs)
        return str_case(self, 3)

    def swapcase(self, *args, **kwargs):
        _noargs('swapcase', args, kwargs)
        return str_case(self, 4)

    def capitalize(self, *args, **kwargs):
        _noargs('capitalize', args, kwargs)
        return str_case(self, 5)

    def center(self, *args):
        width, fill = _fill(args, 'center')
        n = str_len(self)
        if width <= n:
            return _exact(self)
        marg = width - n
        left = marg // 2 + (marg & width & 1)
        return _cat(_pad(fill, left), self, _pad(fill, marg - left))

    def ljust(self, *args):
        width, fill = _fill(args, 'ljust')
        n = str_len(self)
        if width <= n:
            return _exact(self)
        return _cat(self, _pad(fill, width - n))

    def rjust(self, *args):
        width, fill = _fill(args, 'rjust')
        n = str_len(self)
        if width <= n:
            return _exact(self)
        return _cat(_pad(fill, width - n), self)

    def zfill(self, *args, **kwargs):
        _one('zfill', args, kwargs)
        for w in args:
            width = _ssize(w)
        n = str_len(self)
        if width <= n:
            return _exact(self)
        fill = width - n
        if n and (str_ord(self, 0) == 43 or str_ord(self, 0) == 45):
            return _cat(str_item(self, 0), _pad('0', fill), str_slice(self, 1, n))
        return _cat(_pad('0', fill), self)

    def expandtabs(self, *args, **kwargs):
        tabsize = 8
        n = size(args)
        if n > 1:
            error(TypeError, _cat('expandtabs() takes at most 1 argument (', _decimal(n), ' given)'))
        for a in args:
            tabsize = _ssize(a)
        for key, value in ns_items(kwargs):
            if key != 'tabsize':
                error(TypeError, _cat("expandtabs() got an unexpected keyword argument '", key, "'"))
            tabsize = _ssize(value)
        return str_expandtabs(self, tabsize)

    def translate(self, table, /):
        getitem = type_lookup(type(table), '__getitem__')
        if getitem is MISSING:
            error(TypeError, _cat("'", _name(table), "' object is not subscriptable"))
        pieces = []
        for ch in self:
            try:
                v = call_special(getitem, table, str_ord(ch, 0))
            except LookupError:
                list_append(pieces, ch)
                continue
            if v is None:
                continue
            if kind(v, 'int'):
                if v < 0 or v > 0x10ffff:
                    error(ValueError, 'character mapping must be in range(0x110000)')
                list_append(pieces, str_chr(v))
            elif kind(v, 'str'):
                list_append(pieces, v)
            else:
                error(TypeError, 'character mapping must return integer, None or str')
        return str_join('', (*pieces,))

    @classmethod_
    def maketrans(cls, x, y=MISSING, z=MISSING):
        table = {}
        if y is MISSING:
            if z is not MISSING or not kind(x, 'dict'):
                error(TypeError, 'if you give only one argument to maketrans it must be a dict')
            for key, value in x.items():
                if kind(key, 'str'):
                    if str_len(key) != 1:
                        error(ValueError, 'string keys in translate table must be of length 1')
                    key = str_ord(key, 0)
                table[key] = value
            return table
        if not kind(x, 'str') or not kind(y, 'str'):
            error(TypeError, _cat('maketrans() argument ', '1' if not kind(x, 'str') else '2',
                                  ' must be str, not ', _name(x if not kind(x, 'str') else y)))
        if str_len(x) != str_len(y):
            error(ValueError, 'the first two maketrans arguments must have equal length')
        i = 0
        for c in x:
            table[str_ord(c, 0)] = str_ord(y, i)
            i += 1
        if z is not MISSING:
            if not kind(z, 'str'):
                error(TypeError, _cat('maketrans() argument 3 must be str, not ', _name(z)))
            for c in z:
                table[str_ord(c, 0)] = None
        return table

    # formatting

    def format(self, *args, **kwargs):
        return _format.str_format(self, args, kwargs)

    def format_map(self, mapping, /):
        return _format.str_format(self, None, mapping)

    # encoding

    def encode(self, *args, **kwargs):
        encoding = 'utf-8'
        errors = 'strict'
        n = size(args)
        if n > 2:
            error(TypeError, _cat('encode() takes at most 2 arguments (', _decimal(n), ' given)'))
        i = 0
        for a in args:
            if i == 0:
                encoding = a
            else:
                errors = a
            i += 1
        for key, value in ns_items(kwargs):
            if key == 'encoding' and i == 0:
                encoding = value
            elif key == 'errors' and i < 2:
                errors = value
            elif key == 'encoding' or key == 'errors':
                error(TypeError, _cat("argument for encode() given by name ('", key, "') and position"))
            else:
                error(TypeError, _cat("encode() got an unexpected keyword argument '", key, "'"))
        if not kind(encoding, 'str'):
            error(TypeError, _cat("encode() argument 'encoding' must be str, not ", _name(encoding)))
        if not kind(errors, 'str'):
            error(TypeError, _cat("encode() argument 'errors' must be str, not ", _name(errors)))
        return _encode(self, encoding, errors)

    # predicates

    def isascii(self, *args, **kwargs):
        _noargs('isascii', args, kwargs)
        return str_is(self, 11)

    def isalpha(self, *args, **kwargs):
        _noargs('isalpha', args, kwargs)
        return str_is(self, 0)

    def isalnum(self, *args, **kwargs):
        _noargs('isalnum', args, kwargs)
        return str_is(self, 1)

    def isdecimal(self, *args, **kwargs):
        _noargs('isdecimal', args, kwargs)
        return str_is(self, 2)

    def isdigit(self, *args, **kwargs):
        _noargs('isdigit', args, kwargs)
        return str_is(self, 3)

    def isnumeric(self, *args, **kwargs):
        _noargs('isnumeric', args, kwargs)
        return str_is(self, 4)

    def isspace(self, *args, **kwargs):
        _noargs('isspace', args, kwargs)
        return str_is(self, 5)

    def islower(self, *args, **kwargs):
        _noargs('islower', args, kwargs)
        return str_is(self, 6)

    def isupper(self, *args, **kwargs):
        _noargs('isupper', args, kwargs)
        return str_is(self, 7)

    def istitle(self, *args, **kwargs):
        _noargs('istitle', args, kwargs)
        return str_is(self, 8)

    def isprintable(self, *args, **kwargs):
        _noargs('isprintable', args, kwargs)
        return str_is(self, 9)

    def isidentifier(self, *args, **kwargs):
        _noargs('isidentifier', args, kwargs)
        return str_is(self, 10)


def _chars(name, args):
    n = size(args)
    if n > 1:
        error(TypeError, _cat(name, ' expected at most 1 argument, got ', _decimal(n)))
    for a in args:
        if a is not None and not kind(a, 'str'):
            error(TypeError, _cat(name, ' arg must be None or str'))
        return a
    return None


def _removable(x, name):
    if not kind(x, 'str'):
        error(TypeError, _cat(name, '() argument must be str, not ', _name(x)))


def _partition(s, sep, last):
    if not kind(sep, 'str'):
        error(TypeError, _cat('must be str, not ', _name(sep)))
    n = str_len(sep)
    if n == 0:
        error(ValueError, 'empty separator')
    length = str_len(s)
    at = str_find(s, sep, 0, length, last)
    if at < 0:
        empty = str_slice(s, 0, 0)
        if last:
            return (empty, empty, _exact(s))
        return (_exact(s), empty, empty)
    return (str_slice(s, 0, at), _exact(sep), str_slice(s, at + n, length))


operators.add_sequence(str)


@kernel_type(static_type('str_iterator'))
class str_iterator:
    def __iter__(self):
        return self

    def __next__(self):
        v = str_iter_next(self)
        if v is MISSING:
            raise StopIteration
        return v


def repr(obj, /):
    """Return the canonical string representation of the object."""
    return to_repr(obj)


def ascii(obj, /):
    """Return an ASCII-only representation of an object."""
    return to_ascii(obj)


def format(value, format_spec='', /):
    """Return type(value).__format__(value, format_spec)"""
    if not kind(format_spec, 'str'):
        error(TypeError, _cat('format() argument 2 must be str, not ', _name(format_spec)))
    return format_value(value, format_spec)


kernel_set('str', to_str)
kernel_set('repr', to_repr)
kernel_set('ascii', to_ascii)
kernel_set('format', format_value)
