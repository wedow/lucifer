"""_json (Modules/_json.c): the accelerator behind Lib/json, as Python.

Both directions read a str one code point at a time (str_ord): plain integer
loops find the runs that need no work, and the output is sliced from the
original str (str_slice), so the JIT compiles them to tight loops."""

from _lucifer import _args, float_from_text, operators, str_ord, str_slice, type_tp_name

__all__ = ('encode_basestring', 'encode_basestring_ascii', 'make_encoder', 'make_scanner', 'scanstring')

_SSIZE_MAX = 9223372036854775807


def _ssize(x):
    """A Py_ssize_t argument ('n'): index(x), which must fit."""
    n = x if type(x) is int else operators.index(x)
    if n > _SSIZE_MAX or n < -_SSIZE_MAX - 1:
        raise OverflowError('Python int too large to convert to C ssize_t')
    return n


def _check_str(s):
    if not isinstance(s, str):
        raise TypeError('first argument must be a string, not %.80s' % type_tp_name(type(s)))


def _type_name(ob):
    """The fully qualified name of ob's type, as %T prints it."""
    tp = type(ob)
    module = tp.__module__
    if module in ('builtins', '__main__'):
        return tp.__qualname__
    return '%s.%s' % (module, tp.__qualname__)


# ---------------------------------------------------------------------------
# Escaping.

_HEX2 = ['%02x' % b for b in range(256)]


def _uescape(c):
    """The \\uXXXX escape of code point c, a surrogate pair beyond the BMP."""
    if c >= 0x10000:
        c -= 0x10000
        return _uescape(0xd800 | (c >> 10)) + _uescape(0xdc00 | (c & 0x3ff))
    return '\\u' + _HEX2[c >> 8] + _HEX2[c & 255]


# Code point -> its escape, for the characters below 128 that can need one.
_ESCAPE = [None] * 128
for _c in range(0x20):
    _ESCAPE[_c] = _uescape(_c)
for _c, _e in ((8, '\\b'), (12, '\\f'), (10, '\\n'), (13, '\\r'), (9, '\\t'), (34, '\\"'), (92, '\\\\'),
               (127, _uescape(127))):
    _ESCAPE[_c] = _e
del _c, _e


def _escape(s, limit):
    """s quoted, with '"', '\\' and the characters below 32 or from limit
    on escaped."""
    n = len(s)
    chunks = None
    start = 0
    i = 0
    while i < n:
        c = str_ord(s, i)
        if 32 <= c < limit and c != 34 and c != 92:
            i += 1
            continue
        if chunks is None:
            chunks = ['"']
        if start < i:
            chunks.append(str_slice(s, start, i))
        chunks.append(_ESCAPE[c] if c < 128 else _uescape(c))
        i += 1
        start = i
    if chunks is None:
        return '"' + str_slice(s, 0, n) + '"'
    if start < n:
        chunks.append(str_slice(s, start, n))
    chunks.append('"')
    return ''.join(chunks)


def encode_basestring_ascii(pystr, /):
    """encode_basestring_ascii(string) -> string

Return an ASCII-only JSON representation of a Python string"""
    _check_str(pystr)
    return _escape(pystr, 127)


def encode_basestring(pystr, /):
    """encode_basestring(string) -> string

Return a JSON representation of a Python string"""
    _check_str(pystr)
    return _escape(pystr, 0x110000)


# ---------------------------------------------------------------------------
# Decoding.

def _error(msg, s, pos):
    from json.decoder import JSONDecodeError
    raise JSONDecodeError(msg, s, pos)


# Escape character -> its value, for the escapes other than \u.
_BACKSLASH = {34: '"', 92: '\\', 47: '/', 98: '\b', 102: '\f', 110: '\n', 114: '\r', 116: '\t'}


def _hex4(s, i):
    """The value of the four hex digits at s[i:i+4], or -1."""
    v = 0
    end = i + 4
    while i < end:
        d = str_ord(s, i)
        if 48 <= d <= 57:
            v = v * 16 + d - 48
        elif 97 <= d <= 102:
            v = v * 16 + d - 87
        elif 65 <= d <= 70:
            v = v * 16 + d - 55
        else:
            return -1
        i += 1
    return v


def _scanstring(s, end, strict):
    """scanstring_unicode: the JSON string whose opening quote is just
    before end, and the index after its closing quote."""
    n = len(s)
    begin = end - 1
    if end < 0 or n < end:
        raise ValueError('end is out of bounds')
    chunks = None
    while True:
        i = end
        c = 0
        while i < n:
            c = str_ord(s, i)
            if c == 34 or c == 92:
                break
            if c <= 0x1f and strict:
                _error('Invalid control character at', s, i)
            i += 1
        if i == n:
            _error('Unterminated string starting at', s, begin)
        if c == 34 and chunks is None:
            return str_slice(s, end, i), i + 1
        if chunks is None:
            chunks = []
        if i != end:
            chunks.append(str_slice(s, end, i))
        i += 1
        if c == 34:
            return ''.join(chunks), i
        if i == n:
            _error('Unterminated string starting at', s, begin)
        c = str_ord(s, i)
        if c != 117:
            end = i + 1
            e = _BACKSLASH.get(c)
            if e is None:
                _error('Invalid \\escape', s, end - 2)
            chunks.append(e)
            continue
        i += 1
        end = i + 4
        if end > n:
            _error('Invalid \\uXXXX escape', s, i - 1)
        c = _hex4(s, i)
        if c < 0:
            _error('Invalid \\uXXXX escape', s, end - 5)
        if 0xd800 <= c <= 0xdbff and end + 1 < n and str_ord(s, end) == 92 and str_ord(s, end + 1) == 117:
            low = _hex4(s, end + 2) if end + 6 <= n else -1
            if low < 0:
                _error('Invalid \\uXXXX escape', s, end + 1)
            if 0xdc00 <= low <= 0xdfff:
                c = 0x10000 + ((c - 0xd800) << 10) + (low - 0xdc00)
                end += 6
        chunks.append(chr(c))


def scanstring(pystr, end, strict=True, /):
    """scanstring(string, end, strict=True) -> (string, end)

Scan the string s for a JSON string. End is the index of the
character in s after the quote that started the JSON string.
Unescapes all valid JSON string escape sequences and raises ValueError
on attempt to decode an invalid string. If strict is False then literal
control characters are allowed in the string.

Returns a tuple of the decoded string and the index of the character in s
after the end quote."""
    end = _ssize(end)
    strict = True if strict else False
    _check_str(pystr)
    return _scanstring(pystr, end, strict)


def _skip(s, i, n):
    """The index of the first character at or after i that is not JSON whitespace."""
    while i < n:
        c = str_ord(s, i)
        if c != 32 and c != 10 and c != 13 and c != 9:
            return i
        i += 1
    return i


def _scan_once(sc, memo, s, idx):
    """scan_once_unicode: the JSON value at idx, and the index after it."""
    n = len(s)
    if idx >= n:
        raise StopIteration(idx)
    c = str_ord(s, idx)
    if c == 34:
        return _scanstring(s, idx + 1, sc.strict)
    if c == 123:
        return _parse_object(sc, memo, s, idx + 1)
    if c == 91:
        return _parse_array(sc, memo, s, idx + 1)
    if c == 110:
        if idx + 3 < n and str_ord(s, idx + 1) == 117 and str_ord(s, idx + 2) == 108 and str_ord(s, idx + 3) == 108:
            return None, idx + 4
    elif c == 116:
        if idx + 3 < n and str_ord(s, idx + 1) == 114 and str_ord(s, idx + 2) == 117 and str_ord(s, idx + 3) == 101:
            return True, idx + 4
    elif c == 102:
        if (idx + 4 < n and str_ord(s, idx + 1) == 97 and str_ord(s, idx + 2) == 108 and str_ord(s, idx + 3) == 115
                and str_ord(s, idx + 4) == 101):
            return False, idx + 5
    elif c == 78:
        if idx + 3 <= n and str_slice(s, idx, idx + 3) == 'NaN':
            return sc.parse_constant('NaN'), idx + 3
    elif c == 73:
        if idx + 8 <= n and str_slice(s, idx, idx + 8) == 'Infinity':
            return sc.parse_constant('Infinity'), idx + 8
    elif c == 45:
        if idx + 9 <= n and str_slice(s, idx, idx + 9) == '-Infinity':
            return sc.parse_constant('-Infinity'), idx + 9
    return _match_number(sc, s, idx)


def _parse_object(sc, memo, s, idx):
    """_parse_object_unicode: the object whose { is just before idx."""
    n = len(s)
    strict = sc.strict
    pairs_hook = sc.object_pairs_hook
    pairs = [] if pairs_hook is not None else None
    rval = {}
    if idx < n and str_ord(s, idx) <= 32:
        idx = _skip(s, idx, n)
    if idx >= n or str_ord(s, idx) != 125:
        while True:
            if idx >= n or str_ord(s, idx) != 34:
                _error('Expecting property name enclosed in double quotes', s, idx)
            key, idx = _scanstring(s, idx + 1, strict)
            key = memo.setdefault(key, key)
            if idx < n and str_ord(s, idx) <= 32:
                idx = _skip(s, idx, n)
            if idx >= n or str_ord(s, idx) != 58:
                _error("Expecting ':' delimiter", s, idx)
            idx += 1
            if idx < n and str_ord(s, idx) <= 32:
                idx = _skip(s, idx, n)
            value, idx = _scan_once(sc, memo, s, idx)
            if pairs is None:
                rval[key] = value
            else:
                pairs.append((key, value))
            if idx < n and str_ord(s, idx) <= 32:
                idx = _skip(s, idx, n)
            if idx < n and str_ord(s, idx) == 125:
                break
            if idx >= n or str_ord(s, idx) != 44:
                _error("Expecting ',' delimiter", s, idx)
            comma = idx
            idx += 1
            if idx < n and str_ord(s, idx) <= 32:
                idx = _skip(s, idx, n)
            if idx < n and str_ord(s, idx) == 125:
                _error('Illegal trailing comma before end of object', s, comma)
    if pairs is not None:
        return pairs_hook(pairs), idx + 1
    if sc.object_hook is not None:
        return sc.object_hook(rval), idx + 1
    return rval, idx + 1


def _parse_array(sc, memo, s, idx):
    """_parse_array_unicode: the array whose [ is just before idx."""
    n = len(s)
    rval = []
    if idx < n and str_ord(s, idx) <= 32:
        idx = _skip(s, idx, n)
    if idx >= n or str_ord(s, idx) != 93:
        while True:
            value, idx = _scan_once(sc, memo, s, idx)
            rval.append(value)
            if idx < n and str_ord(s, idx) <= 32:
                idx = _skip(s, idx, n)
            if idx < n and str_ord(s, idx) == 93:
                break
            if idx >= n or str_ord(s, idx) != 44:
                _error("Expecting ',' delimiter", s, idx)
            comma = idx
            idx += 1
            if idx < n and str_ord(s, idx) <= 32:
                idx = _skip(s, idx, n)
            if idx < n and str_ord(s, idx) == 93:
                _error('Illegal trailing comma before end of array', s, comma)
    return rval, idx + 1


def _match_number(sc, s, start):
    """_match_number_unicode: the number at start, through parse_int or
    parse_float, or StopIteration(start) where there is none."""
    n = len(s)
    i = start
    c = str_ord(s, i)
    if c == 45:
        i += 1
        if i >= n:
            raise StopIteration(start)
        c = str_ord(s, i)
    if 49 <= c <= 57:
        value = c - 48
        i += 1
        while i < n:
            c = str_ord(s, i)
            if c < 48 or c > 57:
                break
            value = value * 10 + c - 48
            i += 1
    elif c == 48:
        value = 0
        i += 1
    else:
        raise StopIteration(start)
    digits = i - start
    is_float = False
    if i < n - 1 and str_ord(s, i) == 46 and 48 <= str_ord(s, i + 1) <= 57:
        is_float = True
        i += 2
        while i < n and 48 <= str_ord(s, i) <= 57:
            i += 1
    if i < n - 1 and (str_ord(s, i) == 101 or str_ord(s, i) == 69):
        e = i
        i += 1
        if i < n - 1 and (str_ord(s, i) == 45 or str_ord(s, i) == 43):
            i += 1
        while i < n and 48 <= str_ord(s, i) <= 57:
            i += 1
        if 48 <= str_ord(s, i - 1) <= 57:
            is_float = True
        else:
            i = e
    f = sc.parse_float if is_float else sc.parse_int
    if f is int and not is_float and digits <= 18:
        # The digits read above: at most 18 make an exact machine-sized int.
        return (-value if str_ord(s, start) == 45 else value), i
    numstr = str_slice(s, start, i)
    if f is float:
        return float_from_text(numstr), i
    return (int(numstr) if f is int else f(numstr)), i


class Scanner:
    """JSON scanner object"""

    __slots__ = ('strict', 'object_hook', 'object_pairs_hook', 'parse_float', 'parse_int', 'parse_constant')

    def __new__(cls, *args, **kwargs):
        context, = _args.unpack('make_scanner', args, kwargs, ('context',), 0, 1)
        self = object.__new__(cls)
        self.strict = True if context.strict else False
        self.object_hook = context.object_hook
        self.object_pairs_hook = context.object_pairs_hook
        self.parse_float = context.parse_float
        self.parse_int = context.parse_int
        self.parse_constant = context.parse_constant
        return self

    def __call__(self, string, idx):
        idx = _ssize(idx)
        _check_str(string)
        if idx < 0:
            raise ValueError('idx cannot be negative')
        return _scan_once(self, {}, string, idx)


make_scanner = Scanner


# ---------------------------------------------------------------------------
# Encoding.

def _check_separator(position, sep):
    if type(sep) is not str and not isinstance(sep, str):
        raise TypeError('make_encoder() argument %d must be str, not %.50s' % (position, type_tp_name(type(sep))))


class Encoder:
    """Encoder(markers, default, encoder, indent, key_separator, item_separator, sort_keys, skipkeys, allow_nan)"""

    __slots__ = ('markers', 'default', 'encoder', 'indent', 'key_separator', 'item_separator', 'sort_keys',
                 'skipkeys', 'allow_nan')

    def __new__(cls, *args, **kwargs):
        if kwargs or len(args) != 9:
            args = _args.unpack('make_encoder', args, kwargs, Encoder.__slots__, 0, 9)
        markers, default, encoder, indent, key_separator, item_separator, sort_keys, skipkeys, allow_nan = args
        _check_separator(5, key_separator)
        _check_separator(6, item_separator)
        # PyObject_IsTrue, as the 'p' format reads them.
        sort_keys = True if sort_keys else False
        skipkeys = True if skipkeys else False
        allow_nan = True if allow_nan else False
        if markers is not None and not isinstance(markers, dict):
            raise TypeError('make_encoder() argument 1 must be dict or None, not %.200s'
                            % type_tp_name(type(markers)))
        self = object.__new__(cls)
        self.markers = markers
        self.default = default
        self.encoder = encoder
        self.indent = indent
        self.key_separator = key_separator
        self.item_separator = item_separator
        self.sort_keys = sort_keys
        self.skipkeys = skipkeys
        self.allow_nan = allow_nan
        return self

    def __call__(self, obj, _current_indent_level):
        level = _ssize(_current_indent_level)
        cache = None
        if self.indent is not None:
            cache = ['\n' + self.indent * level if level else '\n']
        out = []
        _encode(self, out, obj, 0, cache)
        return (''.join(out),)


make_encoder = Encoder


def _item_separator(enc, level, cache):
    """The separator between items at indent level (>= 1); cache holds, at
    2k, a newline and the indent of level k past the caller's, and at 2k-1
    the item separator before that."""
    if level * 2 > len(cache):
        newline = cache[(level - 1) * 2] + enc.indent
        cache.append(enc.item_separator + newline)
        cache.append(newline)
    return cache[level * 2 - 1]


def _encode_str(enc, s):
    f = enc.encoder
    if f is encode_basestring_ascii:
        return _escape(s, 127)
    if f is encode_basestring:
        return _escape(s, 0x110000)
    r = f(s)
    if not isinstance(r, str):
        raise TypeError('encoder() must return a string, not %.80s' % type_tp_name(type(r)))
    return r


def _int_str(i):
    """int's own repr of i: a subclass's __repr__ does not count."""
    return repr(i) if type(i) is int else int.__repr__(i)


def _encode_float(enc, f):
    r = repr(f) if type(f) is float else float.__repr__(f)
    if r == 'inf' or r == '-inf' or r == 'nan':
        if not enc.allow_nan:
            raise ValueError('Out of range float values are not JSON compliant: %r' % (f,))
        return 'Infinity' if r == 'inf' else '-Infinity' if r == '-inf' else 'NaN'
    return r


def _json_type(obj):
    """The type whose JSON form obj takes: str, int, float, list (for tuples
    too) or dict, or None where only default() can say."""
    t = type(obj)
    if t is str or t is int or t is float or t is list or t is dict:
        return t
    if t is tuple:
        return list
    for base in (str, int, float):
        if isinstance(obj, base):
            return base
    if isinstance(obj, (list, tuple)):
        return list
    if isinstance(obj, dict):
        return dict
    return None


def _encode(enc, out, obj, level, cache):
    """encoder_listencode_obj: append the JSON text of obj to out."""
    if obj is None:
        out.append('null')
        return
    if obj is True:
        out.append('true')
        return
    if obj is False:
        out.append('false')
        return
    t = _json_type(obj)
    if t is str:
        out.append(_encode_str(enc, obj))
    elif t is int:
        out.append(_int_str(obj))
    elif t is float:
        out.append(_encode_float(enc, obj))
    elif t is list:
        _encode_list(enc, out, obj, level, cache)
    elif t is dict:
        _encode_dict(enc, out, obj, level, cache)
    else:
        markers = enc.markers
        ident = _enter(markers, obj)
        new = enc.default(obj)
        try:
            _encode(enc, out, new, level, cache)
        except BaseException as e:
            e.add_note('when serializing %s object' % _type_name(obj))
            raise
        if ident is not None:
            markers.pop(ident)


def _enter(markers, obj):
    """Record obj as being encoded; its id, or None without markers."""
    if markers is None:
        return None
    ident = id(obj)
    if ident in markers:
        raise ValueError('Circular reference detected')
    markers[ident] = obj
    return ident


def _encode_list(enc, out, seq, level, cache):
    if type(seq) is not list and type(seq) is not tuple:
        # A subclass: its stored items, as PySequence_Fast reads them.
        seq = (list if isinstance(seq, list) else tuple).__getitem__(seq, slice(None))
    if not seq:
        out.append('[]')
        return
    markers = enc.markers
    ident = _enter(markers, seq)
    out.append('[')
    separator = enc.item_separator
    if cache is not None:
        level += 1
        separator = _item_separator(enc, level, cache)
        out.append(cache[level * 2])
    i = 0
    while i < len(seq):
        if i:
            out.append(separator)
        try:
            _encode(enc, out, seq[i], level, cache)
        except BaseException as e:
            e.add_note('when serializing %s item %d' % (_type_name(seq), i))
            raise
        i += 1
    if ident is not None:
        markers.pop(ident)
    if cache is not None:
        out.append(cache[(level - 1) * 2])
    out.append(']')


def _encode_dict(enc, out, dct, level, cache):
    if not dict.__len__(dct):
        out.append('{}')
        return
    markers = enc.markers
    ident = _enter(markers, dct)
    out.append('{')
    separator = enc.item_separator
    if cache is not None:
        level += 1
        separator = _item_separator(enc, level, cache)
    if enc.sort_keys or type(dct) is not dict:
        items = list(dct.items())
        if enc.sort_keys:
            items.sort()
        for item in items:
            if not isinstance(item, tuple) or len(item) != 2:
                raise ValueError('items must return 2-tuples')
    else:
        items = dct.items()
    first = True
    for key, value in items:
        t = _json_type(key)
        if t is str:
            keystr = key
        elif t is float:
            keystr = _encode_float(enc, key)
        elif key is True:
            keystr = 'true'
        elif key is False:
            keystr = 'false'
        elif key is None:
            keystr = 'null'
        elif t is int:
            keystr = _int_str(key)
        elif enc.skipkeys:
            continue
        else:
            raise TypeError('keys must be str, int, float, bool or None, not %.100s' % type_tp_name(type(key)))
        if first:
            first = False
            if cache is not None:
                out.append(cache[level * 2])
        else:
            out.append(separator)
        out.append(_encode_str(enc, keystr))
        out.append(enc.key_separator)
        try:
            _encode(enc, out, value, level, cache)
        except BaseException as e:
            e.add_note('when serializing %s item %r' % (_type_name(dct), key))
            raise
    if ident is not None:
        markers.pop(ident)
    if cache is not None and not first:
        out.append(cache[(level - 1) * 2])
    out.append('}')
