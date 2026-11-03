"""array (Modules/arraymodule.c): typed arrays of numbers over a bytearray.
The array is a bytearray subclass, so everything that takes a buffer sees the
packed items (as bytes, with format 'B': memoryview(array) is not typed);
length, indexing, iteration and the methods work in items."""

import struct as _struct

typecodes = 'bBuwhHiIlLqQfd'
_NATIVE = {'b': 'b', 'B': 'B', 'u': 'I', 'w': 'I', 'h': 'h', 'H': 'H', 'i': 'i', 'I': 'I', 'l': 'q', 'L': 'Q',
           'q': 'q', 'Q': 'Q', 'f': 'f', 'd': 'd'}
_LIMITS = {
    'b': (-128, 127, 'signed char is less than minimum', 'signed char is greater than maximum'),
    'B': (0, 255, 'unsigned byte integer is less than minimum', 'unsigned byte integer is greater than maximum'),
    'h': (-32768, 32767, 'signed short integer is less than minimum', 'signed short integer is greater than maximum'),
    'H': (0, 65535, 'unsigned short is less than minimum', 'unsigned short is greater than maximum'),
    'i': (-2**31, 2**31 - 1, 'signed integer is less than minimum', 'signed integer is greater than maximum'),
    'I': (0, 2**32 - 1, "can't convert negative value to unsigned int", 'unsigned int is greater than maximum'),
    'l': (-2**63, 2**63 - 1, 'Python int too large to convert to C long', 'Python int too large to convert to C long'),
    'L': (0, 2**64 - 1, "can't convert negative value to unsigned int",
          'Python int too large to convert to C unsigned long'),
    'q': (-2**63, 2**63 - 1, 'int too big to convert', 'int too big to convert'),
    'Q': (0, 2**64 - 1, "can't convert negative int to unsigned", 'int too big to convert'),
}
_MACHINE = {'b': 1, 'B': 0, 'h': 4, 'H': 2, 'i': 8, 'I': 6, 'l': 12, 'L': 10, 'q': 12, 'Q': 10, 'f': 14, 'd': 16,
            'u': 20, 'w': 20}
_MISSING = object()


def _index(value):
    if isinstance(value, int):
        return value
    try:
        method = type(value).__index__
    except AttributeError:
        raise TypeError("'%s' object cannot be interpreted as an integer" % type(value).__name__) from None
    return method(value)


def _encode(typecode, value):
    """The number to pack for value in an array of typecode."""
    if typecode in 'uw':
        if not isinstance(value, str) or len(value) != 1:
            raise TypeError('array item must be a unicode character, not %s' % type(value).__name__)
        return ord(value)
    if typecode in 'fd':
        if isinstance(value, (int, float)):
            return float(value)
        method = getattr(type(value), '__float__', None)
        if method is None:
            if hasattr(type(value), '__index__'):
                return float(_index(value))
            raise TypeError('must be real number, not %s' % type(value).__name__)
        return method(value)
    value = _index(value)
    low, high, low_message, high_message = _LIMITS[typecode]
    if value < low:
        raise OverflowError(low_message)
    if value > high:
        raise OverflowError(high_message)
    return value


class array(bytearray):
    """array(typecode [, initializer]) -> array

Return a new array whose items are restricted by typecode, and
initialized from the optional initializer value, which must be a list,
a bytes-like object, or iterable over elements of the appropriate type."""

    __slots__ = ('_typecode', '_struct', '__dict__')

    def __new__(cls, *args):
        if not 1 <= len(args) <= 2:
            raise TypeError('array() takes %s 1 argument (%d given)' % ('at least' if not args else 'at most',
                                                                        len(args))
                            if not args else 'array() takes at most 2 arguments (%d given)' % len(args))
        typecode = args[0]
        initializer = args[1] if len(args) > 1 else _MISSING
        if not isinstance(typecode, str) or len(typecode) != 1:
            raise TypeError('array() argument 1 must be a unicode character, not %s' % type(typecode).__name__)
        if typecode not in typecodes:
            raise ValueError('bad typecode (must be b, B, u, w, h, H, i, I, l, L, q, Q, f or d)')
        if typecode == 'u':
            import warnings
            warnings.warn("The 'u' type code is deprecated and will be removed in Python 3.16",
                          DeprecationWarning, 2)
        self = bytearray.__new__(cls)
        self._typecode = typecode
        self._struct = _struct.Struct('<' + _NATIVE[typecode])
        if initializer is not _MISSING:
            if isinstance(initializer, array):
                if (typecode in 'uw') != (initializer._typecode in 'uw'):
                    raise TypeError('cannot use a unicode array to initialize an array with typecode '
                                    "'%s'" % typecode)
                bytearray.extend(self, self._pack(initializer._values()))
            elif isinstance(initializer, (bytes, bytearray)):
                self.frombytes(initializer)
            elif isinstance(initializer, str):
                if typecode not in 'uw':
                    raise TypeError("cannot use a str to initialize an array with typecode '%s'" % typecode)
                self.fromunicode(initializer)
            else:
                self.extend(initializer)
        return self

    def __init__(self, *args):
        pass

    @property
    def typecode(self):
        """the typecode character used to create the array"""
        return self._typecode

    @property
    def itemsize(self):
        """the size, in bytes, of one array item"""
        return self._struct.size

    def _values(self):
        data = bytes(memoryview(self))
        if self._typecode in 'uw':
            return [chr(v) for (v,) in self._struct.iter_unpack(data)]
        return [v for (v,) in self._struct.iter_unpack(data)]

    def _pack(self, values):
        pack = self._struct.pack
        return b''.join(pack(_encode(self._typecode, v)) for v in values)

    def _replace(self, values):
        data = self._pack(values)
        bytearray.clear(self)
        bytearray.extend(self, data)

    def __len__(self):
        return bytearray.__len__(self) // self._struct.size

    def __iter__(self):
        return iter(self._values())

    def __getitem__(self, index):
        if isinstance(index, slice):
            result = array(self._typecode)
            bytearray.extend(result, self._pack(self._values()[index]))
            return result
        index = _index(index)
        n = len(self)
        if index < 0:
            index += n
        if not 0 <= index < n:
            raise IndexError('array index out of range')
        return self._values()[index]

    def __setitem__(self, index, value):
        values = self._values()
        if isinstance(index, slice):
            if not isinstance(value, array):
                raise TypeError('can only assign array (not "%s") to array slice' % type(value).__name__)
            if value._typecode != self._typecode:
                raise TypeError('bad argument type for built-in operation')
            if index.step not in (None, 1) and len(range(*index.indices(len(values)))) != len(value):
                raise ValueError('attempt to assign array of size %d to extended slice of size %d'
                                 % (len(value), len(range(*index.indices(len(values))))))
            values[index] = value._values()
        else:
            index = _index(index)
            if index < 0:
                index += len(values)
            if not 0 <= index < len(values):
                raise IndexError('array assignment index out of range')
            _encode(self._typecode, value)
            values[index] = value
        self._replace(values)

    def __delitem__(self, index):
        values = self._values()
        if isinstance(index, slice):
            del values[index]
        else:
            index = _index(index)
            if index < 0:
                index += len(values)
            if not 0 <= index < len(values):
                raise IndexError('array assignment index out of range')
            del values[index]
        self._replace(values)

    def __contains__(self, value):
        return value in self._values()

    def _same(self, other, operation):
        if not isinstance(other, array):
            raise TypeError('can only extend array with array (not "%s")' % type(other).__name__
                            if operation == 'extend' else 'bad argument type for built-in operation')
        if other._typecode != self._typecode:
            raise TypeError('bad argument type for built-in operation')

    def __add__(self, other):
        self._same(other, 'add')
        result = array(self._typecode)
        bytearray.extend(result, bytes(memoryview(self)) + bytes(memoryview(other)))
        return result

    def __iadd__(self, other):
        self._same(other, 'add')
        bytearray.extend(self, bytes(memoryview(other)))
        return self

    def __mul__(self, count):
        count = _index(count)
        result = array(self._typecode)
        bytearray.extend(result, bytes(memoryview(self)) * max(count, 0))
        return result

    __rmul__ = __mul__

    def __imul__(self, count):
        data = bytes(memoryview(self)) * max(_index(count), 0)
        bytearray.clear(self)
        bytearray.extend(self, data)
        return self

    def _compare(self, other, op):
        if not isinstance(other, array):
            return NotImplemented
        a, b = self._values(), other._values()
        return (a < b, a <= b, a == b, a != b, a > b, a >= b)[op]

    def __lt__(self, other):
        return self._compare(other, 0)

    def __le__(self, other):
        return self._compare(other, 1)

    def __eq__(self, other):
        return self._compare(other, 2)

    def __ne__(self, other):
        return self._compare(other, 3)

    def __gt__(self, other):
        return self._compare(other, 4)

    def __ge__(self, other):
        return self._compare(other, 5)

    __hash__ = None

    def __str__(self):
        return self.__repr__()

    def __repr__(self):
        name = type(self).__name__
        if not len(self):
            return "%s('%s')" % (name, self._typecode)
        if self._typecode in 'uw':
            return "%s('%s', %r)" % (name, self._typecode, ''.join(self._values()))
        return "%s('%s', %r)" % (name, self._typecode, self._values())

    def append(self, value, /):
        """Append new value v to the end of the array."""
        bytearray.extend(self, self._struct.pack(_encode(self._typecode, value)))

    def extend(self, iterable, /):
        """Append items to the end of the array."""
        if isinstance(iterable, array):
            self._same(iterable, 'extend')
            bytearray.extend(self, bytes(memoryview(iterable)))
        else:
            try:
                values = list(iterable)
            except TypeError:
                raise TypeError('can only extend with iterable') from None
            bytearray.extend(self, self._pack(values))

    def fromlist(self, list, /):
        """Append items to array from list."""
        if not isinstance(list, type([])):
            raise TypeError('arg must be list')
        bytearray.extend(self, self._pack(list))

    def frombytes(self, buffer, /):
        """Appends items from the string, interpreting it as an array of machine values."""
        data = bytes(memoryview(buffer))
        if len(data) % self._struct.size:
            raise ValueError('bytes length not a multiple of item size')
        bytearray.extend(self, data)

    def fromunicode(self, ustr, /):
        """Extends this array with data from the unicode string ustr."""
        if self._typecode not in 'uw':
            raise ValueError("fromunicode() may only be called on unicode type arrays ('u' or 'w')")
        if not isinstance(ustr, str):
            raise TypeError('fromunicode() argument must be str, not %s' % type(ustr).__name__)
        bytearray.extend(self, self._pack(ustr))

    def fromfile(self, f, n, /):
        """Read n objects from the file object f and append them to the end of the array."""
        if n < 0:
            raise ValueError('negative count')
        size = n * self._struct.size
        data = f.read(size)
        if not isinstance(data, bytes):
            raise TypeError('read() didn\'t return bytes')
        usable = len(data) - len(data) % self._struct.size
        bytearray.extend(self, data[:usable])
        if len(data) != size:
            raise EOFError('read() didn\'t return enough bytes')

    def tofile(self, f, /):
        """Write all items (as machine values) to the file object f."""
        f.write(bytes(memoryview(self)))

    def tolist(self):
        """Convert array to an ordinary list with the same items."""
        return self._values()

    def tobytes(self):
        """Convert the array to an array of machine values and return the bytes representation."""
        return bytes(memoryview(self))

    def tounicode(self):
        """Extends this array with data from the unicode string ustr."""
        if self._typecode not in 'uw':
            raise ValueError("tounicode() may only be called on unicode type arrays ('u' or 'w')")
        return ''.join(self._values())

    def buffer_info(self):
        """Return a tuple (address, length) giving the current memory address and the length in items of the buffer used to hold array's contents."""
        return (id(self), len(self))

    def byteswap(self):
        """Byteswap all items of the array."""
        size = self._struct.size
        data = bytes(memoryview(self))
        bytearray.clear(self)
        bytearray.extend(self, b''.join(data[i:i + size][::-1] for i in range(0, len(data), size)))

    def count(self, value, /):
        """Return number of occurrences of v in the array."""
        return self._values().count(value)

    def index(self, value, start=0, stop=9223372036854775807, /):
        """Return index of first occurrence of v in the array."""
        values = self._values()
        n = len(values)
        start = start + n if start < 0 else start
        stop = stop + n if stop < 0 else stop
        for i in range(max(start, 0), min(stop, n)):
            if values[i] == value:
                return i
        raise ValueError('array.index(x): x not in array')

    def insert(self, index, value, /):
        """Insert a new item v into the array before position i."""
        index = _index(index)
        values = self._values()
        _encode(self._typecode, value)
        values.insert(index, value)
        self._replace(values)

    def pop(self, index=-1, /):
        """Return the i-th element and delete it from the array."""
        values = self._values()
        if not values:
            raise IndexError('pop from empty array')
        index = _index(index)
        if index < 0:
            index += len(values)
        if not 0 <= index < len(values):
            raise IndexError('pop index out of range')
        value = values.pop(index)
        self._replace(values)
        return value

    def remove(self, value, /):
        """Remove the first occurrence of v in the array."""
        values = self._values()
        try:
            values.remove(value)
        except ValueError:
            raise ValueError('array.remove(x): x not in array') from None
        self._replace(values)

    def reverse(self):
        """Reverse the order of the items in the array."""
        self._replace(self._values()[::-1])

    def __copy__(self):
        return self[:]

    def __deepcopy__(self, memo, /):
        return self[:]

    def __reduce_ex__(self, protocol, /):
        state = getattr(self, '__dict__', None) or None
        if protocol < 3:
            return type(self), (self._typecode, self._values()), state
        return (_array_reconstructor,
                (type(self), self._typecode, _MACHINE[self._typecode], bytes(memoryview(self))), state)

    def __sizeof__(self):
        return 64 + bytearray.__len__(self)

    def __class_getitem__(cls, item):
        import types
        return types.GenericAlias(cls, item)


def _array_reconstructor(arraytype, typecode, mformat_code, items, /):
    """Internal. Used for pickling array objects in a machine-independent way."""
    if not (isinstance(arraytype, type) and issubclass(arraytype, array)):
        raise TypeError('first argument must be a type object, not %s' % type(arraytype).__name__)
    if typecode not in typecodes:
        raise ValueError('second argument must be a valid type code')
    if not 0 <= mformat_code <= 21:
        raise ValueError('third argument must be a valid machine format code.')
    result = arraytype(typecode)
    if mformat_code == _MACHINE[typecode]:
        result.frombytes(items)
        return result
    formats = {0: 'B', 1: 'b', 2: '<H', 3: '>H', 4: '<h', 5: '>h', 6: '<I', 7: '>I', 8: '<i', 9: '>i',
               10: '<Q', 11: '>Q', 12: '<q', 13: '>q', 14: '<f', 15: '>f', 16: '<d', 17: '>d',
               18: '<H', 19: '>H', 20: '<I', 21: '>I'}
    fmt = formats[mformat_code]
    size = _struct.calcsize(fmt)
    if len(items) % size:
        raise ValueError('string length not a multiple of item size')
    values = [v for (v,) in _struct.iter_unpack(fmt, items)]
    if typecode in 'uw':
        values = [chr(v) for v in values]
    result.extend(values)
    return result


ArrayType = array
array.__module__ = 'array'
