"""zlib (Modules/zlibmodule.c): compression and CRCs over the system zlib,
reached through the _lucifer zlib_* primitives, which report each call's
status and message. This module owns the policy: argument checks, the error
class and its messages, and the streaming objects."""

import _lucifer

MAX_WBITS = 15
DEFLATED = 8
DEF_MEM_LEVEL = 8
DEF_BUF_SIZE = 16384
Z_NO_COMPRESSION = 0
Z_BEST_SPEED = 1
Z_BEST_COMPRESSION = 9
Z_DEFAULT_COMPRESSION = -1
Z_FILTERED = 1
Z_HUFFMAN_ONLY = 2
Z_RLE = 3
Z_FIXED = 4
Z_DEFAULT_STRATEGY = 0
Z_NO_FLUSH = 0
Z_PARTIAL_FLUSH = 1
Z_SYNC_FLUSH = 2
Z_FULL_FLUSH = 3
Z_FINISH = 4
Z_BLOCK = 5
Z_TREES = 6
ZLIB_VERSION, ZLIB_RUNTIME_VERSION = _lucifer.zlib_version()
__version__ = '1.0'

_Z_OK, _Z_STREAM_END, _Z_NEED_DICT = 0, 1, 2
_Z_STREAM_ERROR, _Z_DATA_ERROR, _Z_BUF_ERROR, _Z_VERSION_ERROR = -2, -3, -5, -6


class error(Exception):
    pass


def _fail(status, text, message=None):
    """zlib_error: Error <status> <text>[: <zlib's message>]"""
    if status == _Z_VERSION_ERROR:
        message = 'library version mismatch'
    elif message is None:
        message = {_Z_BUF_ERROR: 'incomplete or truncated stream', _Z_STREAM_ERROR: 'inconsistent stream state',
                   _Z_DATA_ERROR: 'invalid input data'}.get(status)
    if message is None:
        raise error('Error %d %s' % (status, text))
    raise error('Error %d %s: %.200s' % (status, text, message))


def _buffer(data):
    """The bytes-like argument as a contiguous byte view."""
    try:
        view = memoryview(data)
    except TypeError:
        raise TypeError("a bytes-like object is required, not '%s'" % type(data).__name__) from None
    if not view.contiguous:
        raise BufferError('memoryview: underlying buffer is not C-contiguous')
    return view.cast('B') if view.format != 'B' or view.ndim != 1 else view


def _unsigned(value):
    return value & 0xFFFFFFFF


def crc32(data, value=0, /):
    """Compute a CRC-32 checksum of data."""
    return _lucifer.zlib_crc32(_buffer(data), _unsigned(value))


def adler32(data, value=1, /):
    """Compute an Adler-32 checksum of data."""
    return _lucifer.zlib_adler32(_buffer(data), _unsigned(value))


def _deflate_stream(level, method, wbits, memlevel, strategy, zdict):
    stream, status = _lucifer.zlib_deflate_init(level, method, wbits, memlevel, strategy, zdict)
    if status == _Z_STREAM_ERROR:
        raise ValueError('Invalid initialization option')
    if status != _Z_OK:
        _fail(status, 'while creating compression object' if zdict is None else 'while setting zdict')
    return stream


def _inflate_stream(wbits, zdict):
    stream, status = _lucifer.zlib_inflate_init(wbits, zdict)
    if status == _Z_STREAM_ERROR:
        raise ValueError('Invalid initialization option')
    if status != _Z_OK:
        _fail(status, 'while creating decompression object' if zdict is None else 'while setting zdict')
    return stream


def compress(data, /, level=Z_DEFAULT_COMPRESSION, wbits=MAX_WBITS):
    """Returns a bytes object containing compressed data."""
    try:
        stream = _deflate_stream(level, DEFLATED, wbits, DEF_MEM_LEVEL, Z_DEFAULT_STRATEGY, None)
    except ValueError:
        raise error('Bad compression level') from None
    out, status, message = _lucifer.zlib_deflate(stream, _buffer(data), Z_FINISH)
    if status != _Z_STREAM_END:
        _fail(status, 'while compressing data', message)
    return out


def decompress(data, /, wbits=MAX_WBITS, bufsize=DEF_BUF_SIZE):
    """Returns a bytes object containing the uncompressed data."""
    if bufsize < 0:
        raise ValueError('bufsize must be non-negative')
    stream = _inflate_stream(wbits, None)
    out, status, message, unread, need_dict = _lucifer.zlib_inflate(stream, _buffer(data), 0, Z_FINISH)
    if status == _Z_NEED_DICT:
        _fail(status, 'while decompressing data', message)
    if status != _Z_STREAM_END:
        _fail(status, 'while decompressing data', message)
    return out


class Compress:
    """A compression object, from compressobj()."""

    def __init__(self, stream):
        self._stream = stream

    def compress(self, data, /):
        """Returns a bytes object containing compressed data for the next part of the input."""
        out, status, message = _lucifer.zlib_deflate(self._stream, _buffer(data), Z_NO_FLUSH)
        if status != _Z_OK:
            _fail(status, 'while compressing data', message)
        return out

    def flush(self, mode=Z_FINISH, /):
        """Return a bytes object containing any remaining compressed data."""
        if mode == Z_NO_FLUSH:
            return b''
        out, status, message = _lucifer.zlib_deflate(self._stream, b'', mode)
        if status not in (_Z_OK, _Z_STREAM_END, _Z_BUF_ERROR):
            _fail(status, 'while flushing', message)
        return out

    def copy(self):
        """Return a copy of the compression object."""
        stream, status = _lucifer.zlib_copy(self._stream)
        if status != _Z_OK:
            _fail(status, 'while copying compression object')
        return Compress(stream)

    __copy__ = copy

    def __deepcopy__(self, memo, /):
        return self.copy()


def compressobj(level=Z_DEFAULT_COMPRESSION, method=DEFLATED, wbits=MAX_WBITS, memLevel=DEF_MEM_LEVEL,
                strategy=Z_DEFAULT_STRATEGY, zdict=None):
    """Return a compressor object."""
    if zdict is not None:
        zdict = _buffer(zdict)
    return Compress(_deflate_stream(level, method, wbits, memLevel, strategy, zdict))


class Decompress:
    """A decompression object, from decompressobj()."""

    def __init__(self, stream, zdict):
        self._stream = stream
        self._zdict = zdict
        self.unused_data = b''
        self.unconsumed_tail = b''
        self.eof = False

    def _run(self, data, max_length, flush):
        out, status, message, unread, need_dict = _lucifer.zlib_inflate(self._stream, data, max_length, flush)
        if status == _Z_NEED_DICT and self._zdict is not None:
            status = _lucifer.zlib_set_dictionary(self._stream, self._zdict)
            if status != _Z_OK:
                _fail(status, 'while setting zdict')
            more, status, message, unread, need_dict = _lucifer.zlib_inflate(
                self._stream, data[len(data) - unread:], max_length, flush)
            out += more
        if status == _Z_STREAM_END:
            self.eof = True
            self.unused_data += bytes(data[len(data) - unread:])
            self.unconsumed_tail = b''
        elif status not in (_Z_OK, _Z_BUF_ERROR):
            _fail(status, 'while decompressing data', message)
        else:
            self.unconsumed_tail = bytes(data[len(data) - unread:])
        return out

    def decompress(self, data, /, max_length=0):
        """Return a bytes object containing the decompressed version of the data."""
        if max_length < 0:
            raise ValueError('max_length must be non-negative')
        if self.eof:
            self.unused_data += bytes(_buffer(data))
            return b''
        return self._run(_buffer(data), max_length, Z_SYNC_FLUSH)

    def flush(self, length=DEF_BUF_SIZE, /):
        """Return a bytes object containing any remaining decompressed data."""
        if length <= 0:
            raise ValueError('length must be greater than zero')
        data = self.unconsumed_tail
        out = self._run(memoryview(data), 0, Z_FINISH)
        return out

    def copy(self):
        """Return a copy of the decompression object."""
        stream, status = _lucifer.zlib_copy(self._stream)
        if status != _Z_OK:
            _fail(status, 'while copying decompression object')
        other = Decompress(stream, self._zdict)
        other.unused_data, other.unconsumed_tail, other.eof = self.unused_data, self.unconsumed_tail, self.eof
        return other

    __copy__ = copy

    def __deepcopy__(self, memo, /):
        return self.copy()


def decompressobj(wbits=MAX_WBITS, zdict=None):
    """Return a decompressor object."""
    if zdict is not None:
        zdict = bytes(_buffer(zdict))
    return Decompress(_inflate_stream(wbits, zdict), zdict)


class _ZlibDecompressor:
    """Create a decompressor object for decompressing data incrementally."""

    def __init__(self, wbits=MAX_WBITS, zdict=None):
        if zdict is not None:
            zdict = bytes(_buffer(zdict))
        self._stream = _inflate_stream(wbits, zdict)
        self._zdict = zdict
        self._pending = b''
        self.eof = False
        self.needs_input = True
        self.unused_data = b''

    def decompress(self, data, /, max_length=-1):
        """Decompress data, returning uncompressed data as bytes."""
        if self.eof:
            raise EOFError('End of stream already reached')
        data = self._pending + bytes(_buffer(data))
        self._pending = b''
        limit = max(max_length, 0) if max_length >= 0 else 0
        out, status, message, unread, need_dict = _lucifer.zlib_inflate(self._stream, data, limit, Z_SYNC_FLUSH)
        if status == _Z_NEED_DICT and self._zdict is not None:
            status = _lucifer.zlib_set_dictionary(self._stream, self._zdict)
            if status != _Z_OK:
                _fail(status, 'while setting zdict')
            more, status, message, unread, need_dict = _lucifer.zlib_inflate(
                self._stream, data[len(data) - unread:], limit, Z_SYNC_FLUSH)
            out += more
        if status == _Z_STREAM_END:
            self.eof = True
            self.needs_input = False
            self.unused_data = data[len(data) - unread:]
        elif status not in (_Z_OK, _Z_BUF_ERROR):
            _fail(status, 'while decompressing data', message)
        else:
            self._pending = data[len(data) - unread:]
            self.needs_input = not unread and not (max_length >= 0 and len(out) >= max_length)
        return out
