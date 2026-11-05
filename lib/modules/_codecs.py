"""_codecs (Modules/_codecsmodule.c, Python/codecs.c): the codec registry, the
error handlers and the built-in codecs.

utf-8, latin-1 and ascii run on the _lucifer.str_encode/str_decode
primitives, which also know the handlers strict, ignore, replace,
backslashreplace, xmlcharrefreplace, surrogateescape and surrogatepass; any
other handler is called from the loops here, which restart the primitive after
each error. The other codecs are Python loops. utf-7 is not provided.
"""

import _lucifer

_str_encode = _lucifer.str_encode
_str_decode = _lucifer.str_decode

_search_functions = []
_search_cache = {}
_error_handlers = {}
_initialized = False
_PRIMITIVE_HANDLERS = ('strict', 'ignore', 'replace', 'backslashreplace', 'xmlcharrefreplace',
                       'surrogateescape', 'surrogatepass')


# The registry

def _normalize(encoding):
    """Lower case; each run of characters other than letters, digits and
dots becomes one underscore, none at either end (_Py_normalize_encoding)."""
    out = []
    punct = False
    for c in encoding:
        if c.isascii() and (c.isalnum() or c == '.'):
            if punct and out:
                out.append('_')
            punct = False
            out.append(c.lower())
        else:
            punct = True
    return ''.join(out)


def _init():
    global _initialized
    if not _initialized:
        _initialized = True
        __import__('encodings')


def register(search_function, /):
    """Register a codec search function.

Search functions are expected to take one argument, the encoding name in
all lower case letters and with hyphens and spaces converted to underscores,
and return a tuple of functions (encoder, decoder, stream_reader,
stream_writer) (or a CodecInfo object)."""
    _init()
    if not callable(search_function):
        raise TypeError('argument must be callable')
    _search_functions.append(search_function)


def unregister(search_function, /):
    """Unregister a codec search function and clear the registry's cache.

If the search function is not registered, do nothing."""
    if search_function in _search_functions:
        _search_functions.remove(search_function)
    _search_cache.clear()


def lookup(encoding, /):
    """Looks up a codec tuple in the Python codec registry and returns a CodecInfo object."""
    if not isinstance(encoding, str):
        raise TypeError('lookup() argument must be str, not ' + type(encoding).__name__)
    if '\0' in encoding:
        raise ValueError('embedded null character')
    _init()
    name = _normalize(encoding)
    try:
        return _search_cache[name]
    except KeyError:
        pass
    if not _search_functions:
        raise LookupError("no codec search functions registered: can't find encoding")
    for search in _search_functions:
        result = search(name)
        if result is None:
            continue
        if not isinstance(result, tuple) or len(result) != 4:
            raise TypeError('codec search functions must return 4-tuples')
        _search_cache[name] = result
        return result
    raise LookupError('unknown encoding: ' + encoding)


def _lookup_text(encoding, alternate):
    """lookup(encoding), which must be a text encoding."""
    codec = lookup(encoding)
    if type(codec) is tuple:
        return codec
    if not getattr(codec, '_is_text_encoding', True):
        raise LookupError("'%s' is not a text encoding" % encoding
                          + ('; use %s to handle arbitrary codecs' % alternate if alternate else ''))
    return codec


def _check_result(result, what):
    if not isinstance(result, tuple) or len(result) != 2:
        raise TypeError(what + ' must return a tuple (object, integer)')
    return result[0]


def _check_name(function, name, value):
    if not isinstance(value, str):
        raise TypeError("%s() argument '%s' must be str, not %s" % (function, name, type(value).__name__))


def encode(obj, encoding='utf-8', errors='strict'):
    """Encodes obj using the codec registered for encoding.

The default encoding is 'utf-8'.  errors may be given to set a different
error handling scheme.  Default is 'strict' meaning that encoding errors raise
a ValueError.  Other possible values are 'ignore', 'replace' and
'backslashreplace' as well as any other name registered with
codecs.register_error that can handle ValueErrors."""
    _check_name('encode', 'encoding', encoding)
    _check_name('encode', 'errors', errors)
    return _check_result(lookup(encoding)[0](obj, errors), 'encoder')


def decode(obj, encoding='utf-8', errors='strict'):
    """Decodes obj using the codec registered for encoding.

Default encoding is 'utf-8'.  errors may be given to set a different error
handling scheme.  Default is 'strict' meaning that encoding errors raise
a ValueError.  Other possible values are 'ignore', 'replace' and
'backslashreplace' as well as any other name registered with
codecs.register_error that can handle ValueErrors."""
    _check_name('decode', 'encoding', encoding)
    _check_name('decode', 'errors', errors)
    return _check_result(lookup(encoding)[1](obj, errors), 'decoder')


def _encode_text(obj, encoding, errors):
    """str.encode for the encodings the str primitives do not know."""
    codec = _lookup_text(encoding, 'codecs.encode()')
    result = _check_result(codec[0](obj, errors), 'encoder')
    if type(result) is not bytes and not isinstance(result, bytes):
        raise TypeError("'%s' encoder returned '%s' instead of 'bytes'; use codecs.encode() to "
                        "encode to arbitrary types" % (encoding, type(result).__name__))
    return result


def _decode_text(obj, encoding, errors):
    """bytes.decode for the encodings the str primitives do not know."""
    codec = _lookup_text(encoding, 'codecs.decode()')
    result = _check_result(codec[1](obj, errors), 'decoder')
    if not isinstance(result, str):
        raise TypeError("'%s' decoder returned '%s' instead of 'str'; use codecs.decode() to "
                        "decode to arbitrary types" % (encoding, type(result).__name__))
    return result


# Error handlers

def register_error(errors, handler, /):
    """Register the specified error handler under the name errors.

handler must be a callable object, that will be called with an exception
instance containing information about the location of the encoding/decoding
error and must return a (replacement, new position) tuple."""
    if not isinstance(errors, str):
        raise TypeError('register_error() argument 1 must be str, not ' + type(errors).__name__)
    if not callable(handler):
        raise TypeError('handler must be callable')
    _error_handlers[errors] = handler


def lookup_error(name, /):
    """lookup_error(errors) -> handler

Return the error handler for the specified error handling name or raise a
LookupError, if no handler exists under this name."""
    if not isinstance(name, str):
        raise TypeError('lookup_error() argument must be str, not ' + type(name).__name__)
    try:
        return _error_handlers[name]
    except KeyError:
        raise LookupError("unknown error handler name '%s'" % name) from None


def _unregister_error(errors, /):
    """Un-register the specified error handler for the error handling `errors'."""
    if errors in _PRIMITIVE_HANDLERS or errors == 'namereplace':
        raise ValueError("cannot un-register built-in error handler '%s'" % errors)
    return _error_handlers.pop(errors, None) is not None


def _is_error(exc, *kinds):
    return isinstance(exc, kinds)


def _error_range(exc, decode):
    """The exception's start and end as the error handlers see them
    (_PyUnicodeError_GetParams of Objects/exceptions.c): the object checked
    to be the str of an encode or translate error or the bytes of a decode
    one, then the positions clamped into it. The ranges the codecs raise are
    in range, where this changes nothing."""
    obj = exc.object
    if not isinstance(obj, bytes if decode else str):
        raise TypeError("UnicodeError 'object' attribute must be a %s"
                        % ('bytes' if decode else 'string'))
    n = len(obj)
    start = exc.start
    if start < 0:
        start = 0
    if start >= n:
        start = 0 if n == 0 else n - 1
    end = exc.end
    if end < 1:
        end = 1
    if end > n:
        end = n
    return start, end


def _hex_escape(ch):
    c = ord(ch)
    if c >= 0x10000:
        return '\\U%08x' % c
    if c >= 0x100:
        return '\\u%04x' % c
    return '\\x%02x' % c


def _unknown(exc):
    raise TypeError("don't know how to handle %s in error callback" % type(exc).__name__)


def _strict_errors(exc):
    if isinstance(exc, BaseException):
        raise exc
    raise TypeError('codec must pass exception instance')


def _ignore_errors(exc):
    if not isinstance(exc, (UnicodeEncodeError, UnicodeDecodeError, UnicodeTranslateError)):
        _unknown(exc)
    return ('', _error_range(exc, isinstance(exc, UnicodeDecodeError))[1])


def _replace_errors(exc):
    if isinstance(exc, UnicodeEncodeError):
        start, end = _error_range(exc, False)
        return ('?' * (end - start), end)
    if isinstance(exc, UnicodeDecodeError):
        return ('\ufffd', _error_range(exc, True)[1])
    if isinstance(exc, UnicodeTranslateError):
        start, end = _error_range(exc, False)
        return ('\ufffd' * (end - start), end)
    _unknown(exc)


def _xmlcharrefreplace_errors(exc):
    if not isinstance(exc, UnicodeEncodeError):
        _unknown(exc)
    start, end = _error_range(exc, False)
    return (''.join('&#%d;' % ord(c) for c in exc.object[start:end]), end)


def _backslashreplace_errors(exc):
    if isinstance(exc, UnicodeDecodeError):
        start, end = _error_range(exc, True)
        return (''.join('\\x%02x' % b for b in exc.object[start:end]), end)
    if not isinstance(exc, (UnicodeEncodeError, UnicodeTranslateError)):
        _unknown(exc)
    start, end = _error_range(exc, False)
    return (''.join(_hex_escape(c) for c in exc.object[start:end]), end)


def _namereplace_errors(exc):
    if not isinstance(exc, UnicodeEncodeError):
        _unknown(exc)
    try:
        import unicodedata
        name = unicodedata.name
    except ImportError:
        name = None
    start, end = _error_range(exc, False)
    out = []
    for c in exc.object[start:end]:
        try:
            out.append('\\N{%s}' % name(c))
        except (TypeError, ValueError):
            out.append(_hex_escape(c))
    return (''.join(out), end)


def _surrogateescape_errors(exc):
    if isinstance(exc, UnicodeEncodeError):
        out = bytearray()
        for c in exc.object[exc.start:exc.end]:
            o = ord(c)
            if o < 0xdc80 or o > 0xdcff:
                raise exc
            out.append(o & 0xff)
        return (bytes(out), exc.end)
    if isinstance(exc, UnicodeDecodeError):
        data = exc.object
        consumed = 0
        out = []
        while consumed < 4 and exc.start + consumed < exc.end:
            b = data[exc.start + consumed]
            if b < 128:
                break
            out.append(chr(0xdc00 + b))
            consumed += 1
        if consumed == 0:
            raise exc
        return (''.join(out), exc.start + consumed)
    _unknown(exc)


def _surrogatepass_encoding(exc):
    """The width in bits and the byte order (-1 little, 0 native, 1 big) of the
codec an error came from: utf-8, utf-16 or utf-32."""
    name = _normalize(exc.encoding).replace('_', '')
    if name == 'utf8':
        return 8, 0
    for width in (16, 32):
        for suffix, order in (('', 0), ('le', -1), ('be', 1)):
            if name == 'utf%d%s' % (width, suffix):
                return width, order
    return None, 0


def _surrogatepass_errors(exc):
    if not isinstance(exc, (UnicodeEncodeError, UnicodeDecodeError)):
        _unknown(exc)
    width, order = _surrogatepass_encoding(exc)
    if width is None:
        raise exc
    if isinstance(exc, UnicodeEncodeError):
        out = bytearray()
        for c in exc.object[exc.start:exc.end]:
            o = ord(c)
            if o < 0xd800 or o > 0xdfff:
                raise exc
            if width == 8:
                out += bytes((0xe0 | (o >> 12), 0x80 | ((o >> 6) & 0x3f), 0x80 | (o & 0x3f)))
            else:
                out += o.to_bytes(width // 8, 'big' if order > 0 else 'little')
        return (bytes(out), exc.end)
    data = exc.object
    start = exc.start
    size = 3 if width == 8 else width // 8
    chunk = data[start:start + size]
    if len(chunk) < size:
        raise exc
    if width == 8:
        if chunk[0] & 0xf0 != 0xe0 or chunk[1] & 0xc0 != 0x80 or chunk[2] & 0xc0 != 0x80:
            raise exc
        o = ((chunk[0] & 0x0f) << 12) | ((chunk[1] & 0x3f) << 6) | (chunk[2] & 0x3f)
    else:
        o = int.from_bytes(chunk, 'big' if order > 0 else 'little')
    if o < 0xd800 or o > 0xdfff:
        raise exc
    return (chr(o), start + size)


for _name, _function in (
        ('strict', _strict_errors), ('ignore', _ignore_errors), ('replace', _replace_errors),
        ('xmlcharrefreplace', _xmlcharrefreplace_errors),
        ('backslashreplace', _backslashreplace_errors), ('namereplace', _namereplace_errors),
        ('surrogateescape', _surrogateescape_errors), ('surrogatepass', _surrogatepass_errors)):
    _function.__name__ = _name + '_errors'
    _error_handlers[_name] = _function


def _handle(errors, exc, length, what):
    """Calls the handler for exc; returns its replacement and the position to
resume at (unicode_encode_call_errorhandler and the decoding counterpart)."""
    if errors == 'strict':
        raise exc
    result = lookup_error(errors)(exc)
    if (not isinstance(result, tuple) or len(result) != 2 or not isinstance(result[1], int)):
        raise TypeError(what)
    replacement, pos = result
    if pos < 0:
        pos += length
    if pos < 0 or pos > length:
        raise IndexError('position %d from error handler out of bounds' % result[1])
    return replacement, pos


_ENCODE_RESULT = 'encoding error handler must return (str/bytes, int) tuple'
_DECODE_RESULT = 'decoding error handler must return (str, int) tuple'


def _handle_decode(errors, encoding, data, start, end, reason):
    replacement, pos = _handle(errors, UnicodeDecodeError(encoding, data, start, end, reason),
                               len(data), _DECODE_RESULT)
    if not isinstance(replacement, str):
        raise TypeError(_DECODE_RESULT)
    return replacement, pos


def _handle_encode(errors, encoding, text, start, end, reason, encode_one):
    """The bytes an encode handler stands in for, and where to resume.
encode_one(str) encodes a replacement string in the same codec."""
    replacement, pos = _handle(errors, UnicodeEncodeError(encoding, text, start, end, reason),
                               len(text), _ENCODE_RESULT)
    if isinstance(replacement, str):
        replacement = encode_one(replacement)
    elif not isinstance(replacement, bytes):
        raise TypeError(_ENCODE_RESULT)
    return replacement, pos


# Argument checking, as the clinic converters do

def _errors(errors, function):
    if errors is None:
        return 'strict'
    if not isinstance(errors, str):
        raise TypeError('%s() argument 2 must be str or None, not %s' % (function, type(errors).__name__))
    return errors


def _text(function, value):
    if not isinstance(value, str):
        raise TypeError('%s() argument 1 must be str, not %s' % (function, type(value).__name__))
    return value


def _bytes(value):
    if type(value) is bytes:
        return value
    try:
        return bytes(memoryview(value))
    except TypeError:
        raise TypeError("a bytes-like object is required, not '%s'" % type(value).__name__) from None


# The codecs the primitives know: utf-8, latin-1, ascii

def _encode_primitive(name, text, errors):
    if errors in _PRIMITIVE_HANDLERS:
        result = _str_encode(text, name, errors)
        if type(result) is bytes:
            return result
        raise UnicodeEncodeError(name, text, result[0], result[1], result[2])
    out = []
    pos = 0
    while True:
        result = _str_encode(text[pos:] if pos else text, name, 'strict')
        if type(result) is bytes:
            out.append(result)
            return b''.join(out)
        start, end, reason = result[0] + pos, result[1] + pos, result[2]
        out.append(_str_encode(text[pos:start], name, 'strict'))
        replacement, pos = _handle_encode(
            errors, name, text, start, end, reason,
            lambda s: _encode_replacement(name, text, start, end, reason, s))
        out.append(replacement)


def _encode_replacement(name, text, start, end, reason, replacement):
    result = _str_encode(replacement, name, 'strict')
    if type(result) is bytes:
        return result
    raise UnicodeEncodeError(name, text, start, end, reason)


def _utf8_tail(data):
    """The length of an incomplete sequence that ends data: 0 to 3 bytes."""
    for k in (1, 2, 3):
        if len(data) < k:
            break
        result = _str_decode(data[len(data) - k:], 'utf-8', 'strict')
        if type(result) is tuple and result[0] == 0 and result[1] == k \
                and result[2] == 'unexpected end of data':
            return k
    return 0


def _decode_primitive(name, data, errors, final):
    data = _bytes(data)
    tail = 0 if final or name != 'utf-8' else _utf8_tail(data)
    body = data[:len(data) - tail] if tail else data
    if errors in _PRIMITIVE_HANDLERS:
        result = _str_decode(body, name, errors)
        if type(result) is str:
            return result, len(body)
        raise UnicodeDecodeError(name, data, result[0], result[1], result[2])
    out = []
    pos = 0
    while True:
        result = _str_decode(body[pos:] if pos else body, name, 'strict')
        if type(result) is str:
            out.append(result)
            return ''.join(out), len(body)
        start, end, reason = result[0] + pos, result[1] + pos, result[2]
        out.append(_str_decode(body[pos:start], name, 'strict'))
        replacement, pos = _handle_decode(errors, name, data, start, end, reason)
        out.append(replacement)


def utf_8_encode(str, errors=None, /):
    return _encode_primitive('utf-8', _text('utf_8_encode', str), _errors(errors, 'utf_8_encode')), len(str)


def utf_8_decode(data, errors=None, final=False, /):
    return _decode_primitive('utf-8', data, _errors(errors, 'utf_8_decode'), final)


def latin_1_encode(str, errors=None, /):
    return _encode_primitive('latin-1', _text('latin_1_encode', str), _errors(errors, 'latin_1_encode')), len(str)


def latin_1_decode(data, errors=None, /):
    return _decode_primitive('latin-1', data, _errors(errors, 'latin_1_decode'), True)


def ascii_encode(str, errors=None, /):
    return _encode_primitive('ascii', _text('ascii_encode', str), _errors(errors, 'ascii_encode')), len(str)


def ascii_decode(data, errors=None, /):
    return _decode_primitive('ascii', data, _errors(errors, 'ascii_decode'), True)


# utf-16 and utf-32: width 2 or 4 bytes per unit

def _units(text, width):
    """text as code units: surrogate pairs in utf-16, one per character in utf-32."""
    for c in text:
        o = ord(c)
        if width == 2 and o >= 0x10000:
            o -= 0x10000
            yield 0xd800 | (o >> 10)
            yield 0xdc00 | (o & 0x3ff)
        else:
            yield o


def _encode_wide(text, errors, byteorder, width):
    name = 'utf-%d' % (width * 8) + ('' if byteorder == 0 else '-le' if byteorder < 0 else '-be')
    big = byteorder > 0
    out = bytearray()
    if byteorder == 0:
        out += (0xfeff).to_bytes(width, 'little')
    pos = 0
    n = len(text)
    while pos < n:
        c = text[pos]
        if '\ud800' <= c <= '\udfff':
            end = pos + 1
            while end < n and '\ud800' <= text[end] <= '\udfff':
                end += 1
            replacement, pos = _handle_encode(
                errors, name, text, pos, end, 'surrogates not allowed',
                lambda s: _encode_wide(s, 'strict', 1 if big else -1, width))
            out += replacement
            continue
        start = pos
        while pos < n and not '\ud800' <= text[pos] <= '\udfff':
            pos += 1
        for unit in _units(text[start:pos], width):
            out += unit.to_bytes(width, 'big' if big else 'little')
    return bytes(out)


def _decode_wide(data, errors, byteorder, final, width):
    """(text, consumed, byteorder) of utf-16 or utf-32 data."""
    data = _bytes(data)
    n = len(data)
    pos = 0
    if byteorder == 0 and n >= width:
        bom = int.from_bytes(data[:width], 'little')
        if bom == 0xfeff:
            byteorder, pos = -1, width
        elif bom == int.from_bytes((0xfeff).to_bytes(width, 'little'), 'big'):
            byteorder, pos = 1, width
    order = 'big' if byteorder > 0 else 'little'
    name = 'utf-%d-%s' % (width * 8, 'be' if byteorder > 0 else 'le')
    out = []
    while pos < n:
        if n - pos < width:
            if not final:
                break
            replacement, pos = _handle_decode(errors, name, data, pos, n, 'truncated data')
            out.append(replacement)
            continue
        ch = int.from_bytes(data[pos:pos + width], order)
        if width == 4:
            if ch > 0x10ffff:
                reason = 'code point not in range(0x110000)'
            elif 0xd800 <= ch <= 0xdfff:
                reason = 'code point in surrogate code point range(0xd800, 0xe000)'
            else:
                out.append(chr(ch))
                pos += 4
                continue
            replacement, pos = _handle_decode(errors, name, data, pos, pos + 4, reason)
            out.append(replacement)
        elif not 0xd800 <= ch <= 0xdfff:
            out.append(chr(ch))
            pos += 2
        elif ch >= 0xdc00:
            replacement, pos = _handle_decode(errors, name, data, pos, pos + 2, 'illegal encoding')
            out.append(replacement)
        elif n - pos < 4:
            if not final:
                break
            replacement, pos = _handle_decode(errors, name, data, pos, n, 'unexpected end of data')
            out.append(replacement)
        else:
            low = int.from_bytes(data[pos + 2:pos + 4], order)
            if 0xdc00 <= low <= 0xdfff:
                out.append(chr(0x10000 + ((ch & 0x3ff) << 10) + (low & 0x3ff)))
                pos += 4
            else:
                replacement, pos = _handle_decode(errors, name, data, pos, pos + 2,
                                                  'illegal UTF-16 surrogate')
                out.append(replacement)
    return ''.join(out), pos, byteorder


def utf_16_encode(str, errors=None, byteorder=0, /):
    return _encode_wide(_text('utf_16_encode', str), _errors(errors, 'utf_16_encode'), byteorder, 2), len(str)


def utf_16_le_encode(str, errors=None, /):
    return _encode_wide(_text('utf_16_le_encode', str), _errors(errors, 'utf_16_le_encode'), -1, 2), len(str)


def utf_16_be_encode(str, errors=None, /):
    return _encode_wide(_text('utf_16_be_encode', str), _errors(errors, 'utf_16_be_encode'), 1, 2), len(str)


def utf_16_decode(data, errors=None, final=False, /):
    text, consumed, _ = _decode_wide(data, _errors(errors, 'utf_16_decode'), 0, final, 2)
    return text, consumed


def utf_16_le_decode(data, errors=None, final=False, /):
    text, consumed, _ = _decode_wide(data, _errors(errors, 'utf_16_le_decode'), -1, final, 2)
    return text, consumed


def utf_16_be_decode(data, errors=None, final=False, /):
    text, consumed, _ = _decode_wide(data, _errors(errors, 'utf_16_be_decode'), 1, final, 2)
    return text, consumed


def utf_16_ex_decode(data, errors=None, byteorder=0, final=False, /):
    return _decode_wide(data, _errors(errors, 'utf_16_ex_decode'), byteorder, final, 2)


def utf_32_encode(str, errors=None, byteorder=0, /):
    return _encode_wide(_text('utf_32_encode', str), _errors(errors, 'utf_32_encode'), byteorder, 4), len(str)


def utf_32_le_encode(str, errors=None, /):
    return _encode_wide(_text('utf_32_le_encode', str), _errors(errors, 'utf_32_le_encode'), -1, 4), len(str)


def utf_32_be_encode(str, errors=None, /):
    return _encode_wide(_text('utf_32_be_encode', str), _errors(errors, 'utf_32_be_encode'), 1, 4), len(str)


def utf_32_decode(data, errors=None, final=False, /):
    text, consumed, _ = _decode_wide(data, _errors(errors, 'utf_32_decode'), 0, final, 4)
    return text, consumed


def utf_32_le_decode(data, errors=None, final=False, /):
    text, consumed, _ = _decode_wide(data, _errors(errors, 'utf_32_le_decode'), -1, final, 4)
    return text, consumed


def utf_32_be_decode(data, errors=None, final=False, /):
    text, consumed, _ = _decode_wide(data, _errors(errors, 'utf_32_be_decode'), 1, final, 4)
    return text, consumed


def utf_32_ex_decode(data, errors=None, byteorder=0, final=False, /):
    return _decode_wide(data, _errors(errors, 'utf_32_ex_decode'), byteorder, final, 4)


# The escape codecs

_SIMPLE_ESCAPES = {'\\': '\\\\', '\t': '\\t', '\n': '\\n', '\r': '\\r'}
_ESCAPE_VALUES = {'\n': '', '\\': '\\', "'": "'", '"': '"', 'a': '\a', 'b': '\b', 'f': '\f',
                  'n': '\n', 'r': '\r', 't': '\t', 'v': '\v'}
_HEX = '0123456789abcdefABCDEF'


def unicode_escape_encode(str, errors=None, /):
    _errors(errors, 'unicode_escape_encode')
    out = []
    for c in _text('unicode_escape_encode', str):
        o = ord(c)
        if c in _SIMPLE_ESCAPES:
            out.append(_SIMPLE_ESCAPES[c])
        elif 0x20 <= o < 0x7f:
            out.append(c)
        else:
            out.append(_hex_escape(c))
    return ''.join(out).encode('latin-1'), len(str)


def raw_unicode_escape_encode(str, errors=None, /):
    _errors(errors, 'raw_unicode_escape_encode')
    out = []
    for c in _text('raw_unicode_escape_encode', str):
        out.append(c if ord(c) < 0x100 else _hex_escape(c))
    return ''.join(out).encode('latin-1'), len(str)


def _warn_invalid(invalid, prefix):
    """The DeprecationWarning for the first invalid escape a decode met."""
    if invalid:
        import _warnings
        char, octal = invalid[0]
        what = 'octal escape sequence' if octal else 'escape sequence'
        _warnings.warn('%s"\\%s" is an invalid %s. Such sequences will not work in the future. '
                       % (prefix, '%o' % char if octal else char, what), DeprecationWarning, 1)


def _escape_decode(name, data, errors, final, raw):
    """unicode_escape and raw_unicode_escape decoding: (text, consumed)."""
    invalid = []
    result = _escape_decode_core(name, data, errors, final, raw, invalid)
    _warn_invalid(invalid, '')
    return result


def _escape_decode_core(name, data, errors, final, raw, invalid):
    data = _bytes(data)
    n = len(data)
    out = []
    pos = 0
    while pos < n:
        b = data[pos]
        if b != 0x5c:
            start = pos
            while pos < n and data[pos] != 0x5c:
                pos += 1
            out.append(data[start:pos].decode('latin-1'))
            continue
        start = pos
        if raw:
            run = pos
            while run < n and data[run] == 0x5c:
                run += 1
            if (run - pos) % 2 == 0 or run >= n or data[run] not in b'uU':
                out.append('\\' * (run - pos))
                pos = run
                continue
            out.append('\\' * (run - pos - 1))
            pos = run - 1 + 1
            kind = chr(data[pos])
            start = pos - 1
        else:
            pos += 1
            if pos >= n:
                if not final:
                    return ''.join(out), start
                replacement, pos = _handle_decode(errors, name, data, start, n, '\\ at end of string')
                out.append(replacement)
                continue
            kind = chr(data[pos])
        pos += 1
        reason = None
        if not raw and kind in _ESCAPE_VALUES:
            out.append(_ESCAPE_VALUES[kind])
            continue
        if not raw and kind in '01234567':
            end = pos
            while end < n and end < pos + 2 and chr(data[end]) in '01234567':
                end += 1
            value = int(data[pos - 1:end], 8)
            if value > 0xff and not invalid:
                invalid.append((value, True))
            out.append(chr(value))
            pos = end
            continue
        if kind in 'xuU':
            digits = {'x': 2, 'u': 4, 'U': 8}[kind]
            text = data[pos:pos + digits].decode('latin-1')
            if len(text) < digits or any(c not in _HEX for c in text):
                bad = pos
                while bad < n and bad < pos + digits and chr(data[bad]) in _HEX:
                    bad += 1
                if bad == n and bad - pos < digits and not final:
                    return ''.join(out), start
                reason = 'truncated \\%s escape' % {'x': 'xXX', 'u': 'uXXXX', 'U': 'UXXXXXXXX'}[kind]
                end = bad
            else:
                value = int(text, 16)
                if value > 0x10ffff:
                    reason = 'illegal Unicode character'
                    end = pos + digits
                else:
                    out.append(chr(value))
                    pos += digits
                    continue
        elif kind == 'N' and not raw:
            close = data.find(b'}', pos) if pos < n and data[pos] == 0x7b else -1
            if close < 0:
                reason = 'malformed \\N character escape'
                end = pos
            else:
                try:
                    import unicodedata
                    out.append(unicodedata.lookup(data[pos + 1:close].decode('latin-1')))
                    pos = close + 1
                    continue
                except (ImportError, KeyError):
                    reason = 'unknown Unicode character name'
                    end = close + 1
        else:
            if not invalid:
                invalid.append((kind, False))
            out.append('\\' + kind)
            continue
        replacement, pos = _handle_decode(errors, name, data, start, end, reason)
        out.append(replacement)
    return ''.join(out), n


def unicode_escape_decode(data, errors=None, final=True, /):
    return _escape_decode('unicodeescape', data, _errors(errors, 'unicode_escape_decode'), final, False)


def raw_unicode_escape_decode(data, errors=None, final=True, /):
    return _escape_decode('rawunicodeescape', data, _errors(errors, 'raw_unicode_escape_decode'), final, True)


def escape_encode(data, errors=None, /):
    if not isinstance(data, bytes):
        raise TypeError('argument 1 must be bytes, not ' + type(data).__name__)
    out = []
    for b in data:
        c = chr(b)
        if c in '\\\'':
            out.append('\\' + c)
        elif c in _SIMPLE_ESCAPES:
            out.append(_SIMPLE_ESCAPES[c])
        elif 0x20 <= b < 0x7f:
            out.append(c)
        else:
            out.append('\\x%02x' % b)
    return ''.join(out).encode('latin-1'), len(data)


def escape_decode(data, errors=None, /):
    errors = _errors(errors, 'escape_decode')
    invalid = []
    data = data.encode('latin-1') if isinstance(data, str) else _bytes(data)
    n = len(data)
    out = bytearray()
    pos = 0
    while pos < n:
        b = data[pos]
        pos += 1
        if b != 0x5c:
            out.append(b)
            continue
        if pos >= n:
            raise ValueError('Trailing \\ in string')
        c = chr(data[pos])
        pos += 1
        if c in _ESCAPE_VALUES:
            out += _ESCAPE_VALUES[c].encode('latin-1')
        elif c in '01234567':
            end = pos
            while end < n and end < pos + 2 and chr(data[end]) in '01234567':
                end += 1
            value = int(data[pos - 1:end], 8)
            if value > 0xff and not invalid:
                invalid.append((value, True))
            out.append(value & 0xff)
            pos = end
        elif c == 'x':
            digits = data[pos:pos + 2].decode('latin-1')
            if len(digits) == 2 and all(d in _HEX for d in digits):
                out.append(int(digits, 16))
                pos += 2
            elif errors == 'strict':
                raise ValueError('invalid \\x escape at position %d' % (pos - 2))
            elif errors == 'replace':
                out.append(0x3f)
                pos += len(digits) and (1 if digits[0] in _HEX else 0)
            elif errors != 'ignore':
                raise ValueError('decoding error; unknown error handling code: ' + errors)
            else:
                pos += len(digits) and (1 if digits[0] in _HEX else 0)
        else:
            if not invalid:
                invalid.append((c, False))
            out += b'\\' + c.encode('latin-1')
    _warn_invalid(invalid, 'b')
    return bytes(out), n


# charmap

def charmap_build(map, /):
    """Build an encoding map from a decoding table of 256 characters."""
    return {ord(c): i for i, c in enumerate(map)}


def _charmap_byte(mapping, c):
    """What mapping gives for the character c: bytes, or None when undefined."""
    try:
        value = mapping[ord(c)]
    except LookupError:
        return None
    if value is None:
        return None
    if isinstance(value, int):
        if not 0 <= value < 256:
            raise TypeError('character mapping must be in range(256)')
        return bytes((value,))
    if isinstance(value, bytes):
        return value
    raise TypeError('character mapping must return integer, bytes or None, not '
                    + type(value).__name__)


def charmap_encode(str, errors=None, mapping=None, /):
    text = _text('charmap_encode', str)
    errors = _errors(errors, 'charmap_encode')
    if mapping is None:
        return latin_1_encode(text, errors)
    out = []
    pos = 0
    n = len(text)
    while pos < n:
        piece = _charmap_byte(mapping, text[pos])
        if piece is not None:
            out.append(piece)
            pos += 1
            continue
        end = pos + 1
        while end < n and _charmap_byte(mapping, text[end]) is None:
            end += 1

        def encode_one(s):
            parts = []
            for c in s:
                part = _charmap_byte(mapping, c)
                if part is None:
                    raise UnicodeEncodeError('charmap', text, pos, end, 'character maps to <undefined>')
                parts.append(part)
            return b''.join(parts)
        replacement, pos = _handle_encode(errors, 'charmap', text, pos, end,
                                          'character maps to <undefined>', encode_one)
        out.append(replacement)
    return b''.join(out), n


def charmap_decode(data, errors=None, mapping=None, /):
    data = _bytes(data)
    errors = _errors(errors, 'charmap_decode')
    if mapping is None:
        return latin_1_decode(data, errors)
    out = []
    pos = 0
    n = len(data)
    while pos < n:
        b = data[pos]
        if isinstance(mapping, str):
            value = mapping[b] if b < len(mapping) else None
        else:
            try:
                value = mapping[b]
            except LookupError:
                value = None
        if isinstance(value, int):
            if not 0 <= value <= 0x10ffff:
                raise TypeError('character mapping must be in range(0x110000)')
            value = chr(value)
        elif value is not None and not isinstance(value, str):
            raise TypeError('character mapping must return integer, None or str')
        if value is None or value == '\ufffe':
            replacement, pos = _handle_decode(errors, 'charmap', data, pos, pos + 1,
                                              'character maps to <undefined>')
            out.append(replacement)
        else:
            out.append(value)
            pos += 1
    return ''.join(out), n


def readbuffer_encode(data, errors=None, /):
    data = data.encode('utf-8') if isinstance(data, str) else _bytes(data)
    return data, len(data)
