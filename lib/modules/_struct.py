"""_struct (Modules/_struct.c): packing and unpacking of C structs, with the
format compiled once to (code, count, offset) items. Native mode (@) uses
x86-64 Linux sizes and alignment; the standard modes use the fixed sizes."""

import _lucifer

__all__ = ('Struct', 'error', 'calcsize', 'pack', 'pack_into', 'unpack', 'unpack_from',
           'iter_unpack', '_clearcache')

_float_pack, _float_unpack = _lucifer.float_pack, _lucifer.float_unpack


class error(Exception):
    pass


error.__module__ = 'struct'

# code -> (native size, standard size, kind); the native alignment is the native size
_CODES = {
    'x': (1, 1, 'x'), 'c': (1, 1, 'c'), 's': (1, 1, 's'), 'p': (1, 1, 'p'), '?': (1, 1, '?'),
    'b': (1, 1, 'i'), 'B': (1, 1, 'u'), 'h': (2, 2, 'i'), 'H': (2, 2, 'u'),
    'i': (4, 4, 'i'), 'I': (4, 4, 'u'), 'l': (8, 4, 'i'), 'L': (8, 4, 'u'),
    'q': (8, 8, 'i'), 'Q': (8, 8, 'u'), 'n': (8, None, 'i'), 'N': (8, None, 'u'),
    'P': (8, None, 'u'), 'e': (2, 2, 'f'), 'f': (4, 4, 'f'), 'd': (8, 8, 'f'),
}
_MODES = {'@': (True, 'little'), '=': (False, 'little'), '<': (False, 'little'),
          '>': (False, 'big'), '!': (False, 'big')}


def _compile(fmt):
    """(items, size, nvalues, little, native): items are (code, count, offset, size) runs of
    `count` items of `size` bytes, or for 's' and 'p' one string of `count` bytes. A
    repeat count is never expanded, so a huge one costs nothing until data meets it."""
    if isinstance(fmt, bytes):
        fmt = fmt.decode('ascii')
    elif not isinstance(fmt, str):
        raise TypeError('Struct() argument 1 must be a str or bytes object, not %s'
                        % type(fmt).__name__)
    native, order = True, 'little'
    i = 0
    if fmt[:1] in _MODES:
        native, order = _MODES[fmt[0]]
        i = 1
    items = []
    offset = nvalues = 0
    count = None
    while i < len(fmt):
        c = fmt[i]
        i += 1
        if c.isspace():
            continue
        if '0' <= c <= '9':
            count = (count or 0) * 10 + ord(c) - 48
            if count > _SSIZE_MAX:
                raise error('total struct size too long')
            continue
        entry = _CODES.get(c)
        if entry is None or (entry[1] is None and not native):
            raise error('bad char in struct format')
        size = entry[0] if native else entry[1]
        n = 1 if count is None else count
        if native and c not in 'xspc?' and offset % size:
            offset += size - offset % size
        if c in 'sp':
            items.append((c, n, offset, n))
            offset += n
            nvalues += 1
        elif c == 'x':
            offset += n
        elif n:
            items.append((c, n, offset, size))
            offset += n * size
            nvalues += n
        if offset > _SSIZE_MAX:
            raise error('total struct size too long')
        count = None
    if count is not None:
        raise error('repeat count given without format specifier')
    return tuple(items), offset, nvalues, order == 'little', native


def _each(items):
    """The runs one value at a time, as (code, count, offset, size)."""
    for code, count, offset, size in items:
        if code in 'sp':
            yield code, count, offset, size
        else:
            for k in range(count):
                yield code, 1, offset + k * size, size


_SSIZE_MAX = (1 << 63) - 1


def _int(value, code, size, signed):
    if not isinstance(value, int):
        try:
            value = value.__index__()
        except AttributeError:
            raise error('required argument is not an integer') from None
    bits = size * 8
    low, high = (-(1 << bits - 1), (1 << bits - 1) - 1) if signed else (0, (1 << bits) - 1)
    if not low <= value <= high:
        if size == 8 and code in 'qQnNP':
            raise error('argument out of range')
        raise error("'%s' format requires %d <= number <= %d" % (code, low, high))
    return value


def _pack_item(code, count, size, value, little):
    kind = _CODES[code][2]
    if kind in 'iu':
        data = _int(value, code, size, kind == 'i').to_bytes(size, 'little', signed=kind == 'i')
    elif kind == 'f':
        try:
            x = float(value) if not isinstance(value, str) else None
        except (TypeError, ValueError):
            x = None
        if x is None:
            raise error('required argument is not a float')
        data = _float_pack(x, size)
    elif kind == '?':
        data = b'\1' if value else b'\0'
    elif kind == 'c':
        if not isinstance(value, bytes) or len(value) != 1:
            raise error('char format requires a bytes object of length 1')
        return value
    else:
        if not isinstance(value, (bytes, bytearray)):
            raise error("argument for '%s' must be a bytes object" % code)
        if kind == 's':
            return bytes(value[:count]).ljust(count, b'\0')
        n = min(len(value), count - 1, 255) if count else 0
        return (bytes([n]) + bytes(value[:n])).ljust(count, b'\0') if count else b''
    return data if little else data[::-1]


def _unpack_item(code, count, size, data, little):
    kind = _CODES[code][2]
    if kind == 'c':
        return data
    if kind == 's':
        return bytes(data)
    if kind == 'p':
        n = min(data[0], count - 1) if count else 0
        return bytes(data[1:1 + n])
    if kind == '?':
        return data[0] != 0
    if not little:
        data = data[::-1]
    if kind == 'f':
        return _float_unpack(bytes(data))
    return int.from_bytes(data, 'little', signed=kind == 'i')


class Struct:
    """Create a compiled struct object.

  format
    The struct format string.

Return a new Struct object which writes and reads binary data according to
the format string."""

    __slots__ = ('_items', '_flat', '_size', '_nvalues', '_little', '_format', '__weakref__')

    def __init__(self, format):
        self._items, self._size, self._nvalues, self._little, _ = _compile(format)
        self._flat = None
        self._format = format if isinstance(format, str) else format.decode('ascii')

    @property
    def format(self):
        """The struct format string used to create this Struct object."""
        return self._format

    @property
    def size(self):
        """The calculated size of the struct in bytes."""
        return self._size

    def _pack(self, args):
        if len(args) != self._nvalues:
            raise error('pack expected %d items for packing (got %d)' % (self._nvalues, len(args)))
        out = bytearray(self._size)
        flat = self._flat
        if flat is None:
            flat = self._expand()
        for (code, count, offset, size), value in zip(flat, args):
            data = _pack_item(code, count, size, value, self._little)
            out[offset:offset + len(data)] = data
        return bytes(out)

    def pack(self, *args):
        """Return a bytes object containing values v1, v2, ... packed according to the format string."""
        return self._pack(args)

    def pack_into(self, buffer, offset, *args):
        """Pack values v1, v2, ... according to the format string and write the packed bytes into the writable buffer."""
        try:
            view = memoryview(buffer)
        except TypeError:
            raise TypeError('argument must be read-write bytes-like object, not %s'
                            % type(buffer).__name__) from None
        if view.readonly:
            raise TypeError('argument must be read-write bytes-like object, not %s'
                            % type(buffer).__name__)
        length = len(view)
        if offset < 0:
            if offset + length < 0:
                raise error('offset %d out of range for %d-byte buffer' % (offset, length))
            offset += length
        if length - offset < self._size:
            if offset > length:
                raise error('offset %d out of range for %d-byte buffer' % (offset, length))
            raise error('pack_into requires a buffer of at least %d bytes for packing %d bytes at offset %d '
                        '(actual buffer size is %d)' % (self._size + offset, self._size, offset, length))
        view[offset:offset + self._size] = self._pack(args)

    def _expand(self):
        """The runs one value at a time, made at the first pack or unpack:
        the size and count checks before it admit a huge repeat count only
        with that many values at hand."""
        self._flat = tuple(_each(self._items))
        return self._flat

    def _unpack(self, data):
        flat = self._flat
        if flat is None:
            flat = self._expand()
        return tuple(_unpack_item(code, count, size, data[offset:offset + size], self._little)
                     for code, count, offset, size in flat)

    def unpack(self, buffer, /):
        """Return a tuple containing unpacked values.

Unpack according to the format string Struct.format. The buffer's size
in bytes must be Struct.size."""
        data = bytes(memoryview(buffer))
        if len(data) != self._size:
            raise error('unpack requires a buffer of %d bytes' % self._size)
        return self._unpack(data)

    def unpack_from(self, buffer, offset=0):
        """Return a tuple containing unpacked values.

Values are unpacked according to the format string Struct.format.

The buffer's size in bytes, starting at position offset, must be
at least Struct.size.

See the struct module documentation for more information."""
        view = memoryview(buffer)
        length = len(view)
        if offset < 0:
            if offset + length < 0:
                raise error('offset %d out of range for %d-byte buffer' % (offset, length))
            offset += length
        if length - offset < self._size:
            if offset > length:
                raise error('offset %d out of range for %d-byte buffer' % (offset, length))
            raise error('unpack_from requires a buffer of at least %d bytes for unpacking %d bytes at '
                        'offset %d (actual buffer size is %d)' % (self._size + offset, self._size, offset, length))
        return self._unpack(bytes(view[offset:offset + self._size]))

    def iter_unpack(self, buffer, /):
        """Return an iterator yielding tuples unpacked from the given bytes.

The bytes are unpacked according to the format string, like
a repeated invocation of unpack_from()."""
        if self._size == 0:
            raise error('cannot iteratively unpack with a struct of length 0')
        data = bytes(memoryview(buffer))
        if len(data) % self._size:
            raise error('iterative unpacking requires a buffer of a multiple of %d bytes' % self._size)
        return (self._unpack(data[i:i + self._size]) for i in range(0, len(data), self._size))

    def __repr__(self):
        return 'Struct(%r)' % (self._format,)

    def __sizeof__(self):
        return 64 + 32 * len(self._items)


Struct.__module__ = 'struct'

_cache = {}


def _struct(format):
    s = _cache.get(format)
    if s is None:
        if len(_cache) >= 100:
            _cache.clear()
        s = _cache[format] = Struct(format)
    return s


def _clearcache():
    """Clear the internal cache."""
    _cache.clear()


def calcsize(format, /):
    """Return size in bytes of the struct described by the format string."""
    return _struct(format).size


def pack(format, /, *args):
    """pack(format, v1, v2, ...) -> bytes

Return a bytes object containing the values v1, v2, ... packed according
to the format string.  See help(struct) for more on format strings."""
    return _struct(format).pack(*args)


def pack_into(format, buffer, offset, /, *args):
    """pack_into(format, buffer, offset, v1, v2, ...)

Pack the values v1, v2, ... according to the format string and write
the packed bytes into the writable buffer buf starting at offset."""
    return _struct(format).pack_into(buffer, offset, *args)


def unpack(format, buffer, /):
    """Return a tuple containing values unpacked according to the format string.

The buffer's size in bytes must be calcsize(format)."""
    return _struct(format).unpack(buffer)


def unpack_from(format, /, buffer, offset=0):
    """Return a tuple containing values unpacked according to the format string.

The buffer's size, minus offset, must be at least calcsize(format)."""
    return _struct(format).unpack_from(buffer, offset)


def iter_unpack(format, buffer, /):
    """Return an iterator yielding tuples unpacked from the given bytes."""
    return _struct(format).iter_unpack(buffer)
