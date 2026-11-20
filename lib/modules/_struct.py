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
    """(items, size, nvalues, little, native): items are (kind, code, count, offset, size) runs of
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
            items.append((entry[2], c, n, offset, n))
            offset += n
            nvalues += 1
        elif c == 'x':
            offset += n
        elif n:
            items.append((entry[2], c, n, offset, size))
            offset += n * size
            nvalues += n
        if offset > _SSIZE_MAX:
            raise error('total struct size too long')
        count = None
    if count is not None:
        raise error('repeat count given without format specifier')
    return tuple(items), offset, nvalues, order == 'little', native


_SSIZE_MAX = (1 << 63) - 1


def _int(value, code, size, signed):
    is_int = isinstance(value, int)
    if not is_int:
        try:
            value = value.__index__()
        except AttributeError:
            raise error('required argument is not an integer') from None
    bits = size * 8
    low, high = (-(1 << bits - 1), (1 << bits - 1) - 1) if signed else (0, (1 << bits) - 1)
    if not low <= value <= high:
        if code == 'P':
            # Native pointers also accept signed negatives as unsigned bit patterns.
            if -(1 << (bits - 1)) <= value < 0:
                return value + high + 1
            if is_int:
                raise error('int too large to convert')
            c_type = 'long' if value < 0 else 'unsigned long'
            raise OverflowError(f'Python int too large to convert to C {c_type}')
        raise error("'%s' format requires %d <= number <= %d" % (code, low, high))
    return value


def _float_bytes(value, size, little):
    """The size bytes of value as a float, in the struct's order."""
    try:
        x = float(value) if not isinstance(value, str) else None
    except (TypeError, ValueError):
        x = None
    if x is None:
        raise error('required argument is not a float')
    data = _float_pack(x, size)
    return data if little else data[::-1]


def _pack_into(out, items, args, little):
    """Each value of args into out at its offset, a run at a time."""
    k = 0
    for kind, code, count, offset, size in items:
        if kind == 'i' or kind == 'u':
            signed = kind == 'i'
            end = offset + count * size
            while offset < end:
                value = _int(args[k], code, size, signed)
                k += 1
                if size == 1:
                    out[offset] = value & 255
                elif size <= 4:
                    # The bytes of a value that fits a machine word, stored one by one.
                    i = offset if little else offset + size - 1
                    step = 1 if little else -1
                    for _ in range(size):
                        out[i] = value & 255
                        value >>= 8
                        i += step
                else:
                    out[offset:offset + size] = value.to_bytes(size, 'little' if little else 'big', signed=signed)
                offset += size
        elif kind == 'f':
            end = offset + count * size
            while offset < end:
                out[offset:offset + size] = _float_bytes(args[k], size, little)
                k += 1
                offset += size
        elif kind == '?':
            for i in range(offset, offset + count):
                out[i] = 1 if args[k] else 0
                k += 1
        elif kind == 'c':
            for i in range(offset, offset + count):
                value = args[k]
                k += 1
                if not isinstance(value, bytes) or len(value) != 1:
                    raise error('char format requires a bytes object of length 1')
                out[i] = value[0]
        else:
            value = args[k]
            k += 1
            if not isinstance(value, (bytes, bytearray)):
                raise error("argument for '%s' must be a bytes object" % code)
            if kind == 's':
                n = min(len(value), count)
                out[offset:offset + n] = value[:n]
            elif count:
                n = min(len(value), count - 1, 255)
                out[offset] = n
                out[offset + 1:offset + 1 + n] = value[:n]


def _unpack_from(data, base, items, little):
    """The values of items in data, whose first byte is at base."""
    values = []
    for kind, code, count, offset, size in items:
        offset += base
        if kind == 'i' or kind == 'u':
            end = offset + count * size
            if size <= 4:
                # A value that fits a machine word, put together from its bytes.
                half = 1 << (size * 8 - 1)
                while offset < end:
                    if little:
                        i = offset + size - 1
                        value = data[i]
                        while i > offset:
                            i -= 1
                            value = (value << 8) | data[i]
                    else:
                        i = offset
                        value = data[i]
                        while i < offset + size - 1:
                            i += 1
                            value = (value << 8) | data[i]
                    if kind == 'i' and value >= half:
                        value -= half << 1
                    values.append(value)
                    offset += size
            else:
                order = 'little' if little else 'big'
                signed = kind == 'i'
                while offset < end:
                    values.append(int.from_bytes(data[offset:offset + size], order, signed=signed))
                    offset += size
        elif kind == 'f':
            end = offset + count * size
            while offset < end:
                piece = data[offset:offset + size]
                values.append(_float_unpack(piece if little else piece[::-1]))
                offset += size
        elif kind == '?':
            for i in range(offset, offset + count):
                values.append(data[i] != 0)
        elif kind == 'c':
            for i in range(offset, offset + count):
                values.append(data[i:i + 1])
        elif kind == 's':
            values.append(data[offset:offset + count])
        else:
            n = min(data[offset], count - 1) if count else 0
            values.append(data[offset + 1:offset + 1 + n])
    return tuple(values)


class Struct:
    """Create a compiled struct object.

  format
    The struct format string.

Return a new Struct object which writes and reads binary data according to
the format string."""

    __slots__ = ('_items', '_size', '_nvalues', '_little', '_format', '__weakref__')

    def __init__(self, format):
        self._items, self._size, self._nvalues, self._little, _ = _compile(format)
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
        items = self._items
        kind, code, count, offset, size = items[0] if len(items) == 1 else ('', '', 0, 0, 0)
        if size == self._size and (kind == 'i' or kind == 'u'):
            # One number that is the whole struct: its bytes are the result.
            signed = kind == 'i'
            return _int(args[0], code, size, signed).to_bytes(size, 'little' if self._little else 'big', signed=signed)
        if size == self._size and kind == 'f':
            return _float_bytes(args[0], size, self._little)
        out = bytearray(self._size)
        _pack_into(out, items, args, self._little)
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

    def unpack(self, buffer, /):
        """Return a tuple containing unpacked values.

Unpack according to the format string Struct.format. The buffer's size
in bytes must be Struct.size."""
        data = buffer if type(buffer) is bytes else bytes(memoryview(buffer))
        if len(data) != self._size:
            raise error('unpack requires a buffer of %d bytes' % self._size)
        return _unpack_from(data, 0, self._items, self._little)

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
        return _unpack_from(bytes(view[offset:offset + self._size]), 0, self._items, self._little)

    def iter_unpack(self, buffer, /):
        """Return an iterator yielding tuples unpacked from the given bytes.

The bytes are unpacked according to the format string, like
a repeated invocation of unpack_from()."""
        if self._size == 0:
            raise error('cannot iteratively unpack with a struct of length 0')
        data = bytes(memoryview(buffer))
        if len(data) % self._size:
            raise error('iterative unpacking requires a buffer of a multiple of %d bytes' % self._size)
        items, little = self._items, self._little
        return (_unpack_from(data, i, items, little) for i in range(0, len(data), self._size))

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
