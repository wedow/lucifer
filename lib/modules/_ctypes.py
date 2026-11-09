"""_ctypes (Modules/_ctypes): C data types and foreign function calls.

The types, their memory layout and every conversion are Python; the native
half (runtime/prim/ctypes.c) is the dynamic loader, raw memory access and
libffi, driven with plain numbers. An instance's memory is a bytearray it
owns, or a range of its owner's (b_base in CPython terms); the address of
either is a plain int, so from_address and pointers work on any memory."""

import _lucifer
import _struct

__version__ = '1.1.0'
RTLD_LOCAL = 0
CTYPES_MAX_ARGCOUNT = 1024
RTLD_GLOBAL = 0x100
FUNCFLAG_CDECL = 1
FUNCFLAG_PYTHONAPI = 4
FUNCFLAG_USE_ERRNO = 8
FUNCFLAG_USE_LASTERROR = 16
SIZEOF_TIME_T = 8

_read = _lucifer.ct_read
_write = _lucifer.ct_write
_address = _lucifer.ct_address


class ArgumentError(Exception):
    pass


# type code -> (struct format, size). 'z', 'Z', 'P' and 'O' hold a pointer;
# 'u' is a wchar_t, an int32 on this platform; 'g' is a long double.
_CODES = {
    'c': ('c', 1), 'b': ('b', 1), 'B': ('B', 1), 'h': ('h', 2), 'H': ('H', 2), 'i': ('i', 4),
    'I': ('I', 4), 'l': ('q', 8), 'L': ('Q', 8), 'q': ('q', 8), 'Q': ('Q', 8), 'f': ('f', 4),
    'd': ('d', 8), 'g': (None, 16), '?': ('?', 1), 'P': ('Q', 8), 'z': ('Q', 8), 'Z': ('Q', 8),
    'O': ('Q', 8), 'u': ('i', 4), 'v': ('h', 2),
}
_INTEGERS = frozenset('bBhHiIlLqQ')
_FFI = {'c': 'b', 'u': 'i', '?': 'B', 'z': 'P', 'Z': 'P', 'O': 'P', 'v': 'h'}      # type code -> libffi code
_MASK = {code: (1 << (8 * size)) - 1 for code, (_, size) in _CODES.items()}


def _align_up(n, a):
    return (n + a - 1) // a * a


def _index(value):
    try:
        return value.__index__()
    except AttributeError:
        raise TypeError("'%s' object cannot be interpreted as an integer" % type(value).__name__) from None


# ---------------------------------------------------------------- instances

class _CData:
    """The base of every ctypes instance: an address, its size, and what keeps the memory
    (and the objects its pointers refer to) alive."""

    _b_size_ = 0
    _b_align_ = 1

    def __new__(cls, *args, **kwargs):
        self = object.__new__(cls)
        buf = bytearray(cls._b_size_)
        self._b_buf = buf
        self._b_addr = _address(buf) if buf else 0
        self._b_base = None
        self._objects = None
        return self

    @property
    def _b_base_(self):
        return self._b_base

    @property
    def _b_needsfree_(self):
        return int(self._b_base is None)

    def _root(self):
        return self._b_base if self._b_base is not None else self

    def _keep(self, key, obj):
        """Keep obj alive as long as the memory this object lives in."""
        root = self._root()
        if root._objects is None:
            root._objects = {}
        root._objects[key] = obj

    def __reduce__(self):
        raise TypeError('ctypes objects containing pointers cannot be pickled')

    def __ctypes_from_outparam__(self):
        return self

    def __buffer__(self, flags, /):
        return _lucifer.ct_view(self._b_addr, self._b_size_, False)

    def __release_buffer__(self, view, /):
        view.release()


def _at(cls, root, addr):
    """An instance of cls at addr, in the memory of root (None: raw memory)."""
    obj = object.__new__(cls)
    obj._b_buf = None
    obj._b_addr = addr
    obj._b_base = root
    obj._objects = None
    return obj


def _view(obj):
    """(address, root) of the memory a ctypes instance occupies."""
    return obj._b_addr, obj._root()


# ---------------------------------------------------------------- metatypes

class _CDataType(type):
    """The metatype of every ctypes type: construction from raw memory, from a
    buffer, from a library symbol, and array types by multiplication."""

    def from_address(cls, address):
        """C.from_address(address) -> C instance: access a C instance at the specified address."""
        return _at(cls, None, _index(address))

    def from_buffer(cls, obj, offset=0):
        """C.from_buffer(object, offset=0) -> C instance: create a C instance from a writable buffer."""
        view = memoryview(obj)
        if view.readonly:
            raise TypeError('underlying buffer is not writable')
        if not view.contiguous:
            raise TypeError('underlying buffer is not C contiguous')
        if offset < 0:
            raise ValueError('offset cannot be negative')
        if cls._b_size_ > view.nbytes - offset:
            raise ValueError('Buffer size too small (%d instead of at least %d bytes)'
                             % (view.nbytes, cls._b_size_ + offset))
        result = _at(cls, obj, _address(obj) + offset)
        result._objects = {'view': view}
        return result

    def from_buffer_copy(cls, obj, offset=0):
        """C.from_buffer_copy(object, offset=0) -> C instance: create a C instance from a readable buffer."""
        data = bytes(memoryview(obj))
        if offset < 0:
            raise ValueError('offset cannot be negative')
        if cls._b_size_ > len(data) - offset:
            raise ValueError('Buffer size too small (%d instead of at least %d bytes)'
                             % (len(data), cls._b_size_ + offset))
        result = cls.__new__(cls)
        _write(result._b_addr, data[offset:offset + cls._b_size_])
        return result

    def in_dll(cls, dll, name):
        """C.in_dll(dll, name) -> C instance: access a C instance in a dll."""
        address = dlsym(dll._handle, name)
        return _at(cls, None, address)

    def __mul__(cls, length):
        if not isinstance(length, int):
            raise TypeError("'%s' object cannot be interpreted as an integer" % type(length).__name__)
        if length < 0:
            raise ValueError('Array length must be >= 0, not %d' % length)
        return _array_type(cls, length)

    __rmul__ = __mul__

    def from_param(cls, value):
        if isinstance(value, cls):
            return value
        if hasattr(value, '_as_parameter_'):
            return cls.from_param(value._as_parameter_)
        raise TypeError('expected %s instance instead of %s' % (cls.__name__, type(value).__name__))


_CDataType.__module__ = '_ctypes'
_array_cache = {}


def _array_type(item, length):
    key = (item, length)
    cls = _array_cache.get(key)
    if cls is None:
        cls = _array_cache[key] = PyCArrayType('%s_Array_%d' % (item.__name__, length), (Array,),
                                               {'_type_': item, '_length_': length})
    return cls


def sizeof(obj_or_type):
    """Return the size in bytes of a ctypes type or instance."""
    size = getattr(obj_or_type, '_b_size_', None)
    if size is None or not isinstance(type(obj_or_type), _CDataType) and not isinstance(obj_or_type, _CDataType):
        raise TypeError('this type has no size')
    return size


def alignment(obj_or_type):
    """Return the alignment requirements of a ctypes type."""
    if isinstance(obj_or_type, (_CDataType, _CData)):
        return obj_or_type._b_align_
    raise TypeError('no alignment info')


def addressof(obj):
    """Return the address of the C instance internal buffer"""
    if not isinstance(obj, _CData):
        raise TypeError('invalid type')
    return obj._b_addr


class CArgObject:
    """byref(obj, offset=0): a lightweight pointer to obj, for function calls."""

    def __init__(self, obj, offset):
        self._obj = obj
        self.offset = offset

    def __repr__(self):
        return '<cparam \'P\' (%#x)>' % (self._obj._b_addr + self.offset)


def byref(obj, offset=0, /):
    """Return a pointer lookalike to a C instance, only usable as function argument."""
    if not isinstance(obj, _CData):
        raise TypeError('byref() argument 1 must be _ctypes._CData, not %s' % type(obj).__name__)
    return CArgObject(obj, offset)


def resize(obj, size):
    """Resize the memory buffer of a ctypes instance"""
    if not isinstance(obj, _CData):
        raise TypeError('expected ctypes instance')
    if size < obj._b_size_:
        raise ValueError('minimum size is %d' % obj._b_size_)
    if obj._b_base is not None or obj._b_buf is None:
        raise ValueError('Memory cannot be resized because this object doesn\'t own it')
    buf = bytearray(size)
    buf[:len(obj._b_buf)] = obj._b_buf
    obj._b_buf = buf
    obj._b_addr = _address(buf)


# ------------------------------------------------------------ simple types

class _Empty:
    pass


_NOVALUE = _Empty()


def _check_code(name, ns, bases):
    code = ns.get('_type_')
    if code is None:
        if any(isinstance(b, PyCSimpleType) and hasattr(b, '_type_') for b in bases):
            return None
        raise AttributeError("class must define a '_type_' attribute")
    if not isinstance(code, str):
        raise TypeError('class must define a \'_type_\' string attribute')
    if len(code) != 1:
        raise ValueError('class must define a \'_type_\' attribute which must be a string of length 1')
    if code not in _CODES:
        raise AttributeError("class must define a '_type_' attribute which must be\na single character string containing one of 'cbBhHiIlLdfuzZqQPXOv?g'.")
    return code


def _encode(code, value, swapped=''):
    """The bytes of a Python value for a simple type code."""
    fmt, size = _CODES[code]
    if code == 'c':
        if isinstance(value, int) and not isinstance(value, bool) and 0 <= value < 256:
            return bytes((value,))
        if isinstance(value, (bytes, bytearray)) and len(value) == 1:
            return bytes(value)
        raise TypeError('one character bytes, bytearray, or an integer in range(256) expected, not %s'
                        % ('%s of length %d' % (type(value).__name__, len(value)) if isinstance(value, (bytes, bytearray))
                           else type(value).__name__))
    if code == 'u':
        if not isinstance(value, str) or len(value) != 1:
            raise TypeError('a unicode character expected, not a string of length %d' % len(value)
                            if isinstance(value, str) else
                            'a unicode character expected, not instance of %s' % type(value).__name__)
        return _struct.pack('<I', ord(value))
    if code == '?':
        return b'\x01' if value else b'\x00'
    if code == 'v':                       # VARIANT_BOOL: true is -1
        return b'\xff\xff' if value else b'\x00\x00'
    if code in 'fd':
        try:
            return _struct.pack(swapped + fmt, value)
        except _struct.error:
            raise TypeError('must be real number, not %s' % type(value).__name__) from None
    if code == 'g':
        if not isinstance(value, (int, float)):
            raise TypeError('must be real number, not %s' % type(value).__name__)
        return _lucifer.ct_longdouble_pack(float(value))
    if code in _INTEGERS:
        return (_index(value) & _MASK[code]).to_bytes(size, 'little' if swapped != '>' else 'big')
    raise TypeError('invalid type code %r' % code)


def _decode(code, data, swapped=''):
    fmt, size = _CODES[code]
    if code == 'c':
        return bytes(data)
    if code == 'u':
        return chr(_struct.unpack('<I', data)[0] & 0x10FFFF) if _struct.unpack('<I', data)[0] < 0x110000 else '�'
    if code in '?v':
        return data != bytes(size)
    if code == 'g':
        return _lucifer.ct_longdouble(data)
    if code in 'fd':
        return _struct.unpack(swapped + fmt, data)[0]
    return int.from_bytes(data, 'little' if swapped != '>' else 'big', signed=code in 'bhilq')


def _pointer_value(value, keep=None):
    """An address from None, an int, bytes, str, an array, a pointer, a byref or a function."""
    if value is None:
        return 0
    if isinstance(value, bool):
        raise TypeError('cannot be converted to pointer')
    if isinstance(value, int):
        return value & 0xffffffffffffffff     # a negative int wraps (PyLong_AsVoidPtr)
    if isinstance(value, CArgObject):
        return value._obj._b_addr + value.offset
    if isinstance(value, (bytes, bytearray)):
        if keep is not None:
            keep.append(value)
        return _address(value)
    if isinstance(value, str):
        wide = bytearray(value.encode('utf-32-le') + b'\0\0\0\0')
        if keep is not None:
            keep.append(wide)
        return _address(wide)
    if isinstance(value, Array):
        return value._b_addr
    if isinstance(value, (_Pointer, CFuncPtr)) or (isinstance(value, _SimpleCData) and value._b_code_ in 'zZP'):
        return int.from_bytes(_read(value._b_addr, 8), 'little')
    raise TypeError('cannot be converted to pointer')


class PyCSimpleType(_CDataType):
    def __new__(mcs, name, bases, ns, **kwargs):
        code = _check_code(name, ns, bases) if name != '_SimpleCData' else None
        cls = super().__new__(mcs, name, bases, ns, **kwargs)
        if code is not None:
            cls._b_code_ = code
            cls._b_size_ = cls._b_align_ = _CODES[code][1]
            if not getattr(cls, '_swapped_', False) and '__ctype_be__' not in ns:
                other = super().__new__(mcs, name, (cls,), {'_swapped_': '>', '__module__': cls.__module__})
                cls.__ctype_be__, cls.__ctype_le__ = other, cls
                other.__ctype_be__, other.__ctype_le__ = other, cls
        return cls

    def from_param(cls, value):
        if isinstance(value, cls):
            return value
        code = cls._b_code_
        if code in 'zZP':
            if _pointer_compatible(code, value):
                param = cls()
                keep = []
                _write(param._b_addr, _pointer_value(value, keep).to_bytes(8, 'little'))
                param._keep('param', (value, keep))
                return param
        elif hasattr(value, '_as_parameter_'):
            return cls.from_param(value._as_parameter_)
        else:
            try:
                return cls(value)
            except TypeError as e:
                if code == 'O':
                    raise
                raise TypeError("'%s' object cannot be interpreted as ctypes.%s" % (type(value).__name__, cls.__name__)) \
                    if isinstance(value, (str, bytes, type(None), tuple, list, dict)) else e
        if hasattr(value, '_as_parameter_'):
            return cls.from_param(value._as_parameter_)
        raise TypeError("'%s' object cannot be interpreted as ctypes.%s" % (type(value).__name__, cls.__name__))


def _pointer_compatible(code, value):
    """Whether a pointer-typed simple type (c_char_p, c_wchar_p, c_void_p) takes value as an argument."""
    if value is None:
        return True
    if code == 'P':
        return isinstance(value, (int, bytes, str, Array, _Pointer, CFuncPtr, CArgObject)) \
            or isinstance(value, _SimpleCData) and value._b_code_ in 'zZP'
    text, char = (bytes, 'c') if code == 'z' else (str, 'u')
    if isinstance(value, text) or isinstance(value, _SimpleCData) and value._b_code_ == code:
        return True
    if isinstance(value, (Array, _Pointer)):
        return getattr(value._type_, '_b_code_', None) == char
    return isinstance(value, CArgObject) and getattr(type(value._obj), '_b_code_', None) == char


PyCSimpleType.__module__ = '_ctypes'


def _simple_get(cls, root, addr):
    code = cls._b_code_
    raw = _read(addr, cls._b_size_)
    swapped = getattr(cls, '_swapped_', '')
    if code in 'zZ':
        pointer = int.from_bytes(raw, 'little')
        if pointer == 0:
            return None
        n = _lucifer.ct_strlen(pointer, code == 'Z')
        return _read(pointer, n) if code == 'z' else _read(pointer, n * 4).decode('utf-32-le', 'surrogatepass')
    if code == 'P':
        return int.from_bytes(raw, 'little') or None
    if code == 'O':
        pointer = int.from_bytes(raw, 'little')
        if pointer == 0:
            raise ValueError('PyObject is NULL')
        return _lucifer.ct_object_at(pointer)
    return _decode(code, raw, swapped)


class _SimpleCData(_CData, metaclass=PyCSimpleType):
    """Base class for primitive data types."""

    def __init__(self, value=_NOVALUE, /):
        if value is not _NOVALUE:
            self.value = value

    @property
    def value(self):
        return _simple_get(type(self), self._root(), self._b_addr)

    @value.setter
    def value(self, value):
        _simple_set(type(self), self, self._b_addr, value)

    def __bool__(self):
        code = self._b_code_
        if code in 'zZP':
            return int.from_bytes(_read(self._b_addr, 8), 'little') != 0
        return bool(self.value)

    def __repr__(self):
        cls = type(self)
        if cls.__bases__[0] is not _SimpleCData:
            return '<%s object at %#x>' % (cls.__name__, id(self))
        return '%s(%r)' % (cls.__name__, self.value)

    def __ctypes_from_outparam__(self):
        return self.value if type(self).__bases__[0] is _SimpleCData else self


def _simple_set(cls, owner, addr, value):
    code = cls._b_code_
    if code in 'zZ':
        if value is None:
            _write(addr, bytes(8))
            return
        if code == 'z' and not isinstance(value, (bytes, int)) or code == 'Z' and not isinstance(value, (str, int)):
            raise TypeError('bytes or integer address expected instead of %s instance' % type(value).__name__
                            if code == 'z' else 'unicode string or integer address expected instead of %s instance'
                            % type(value).__name__)
        keep = []
        pointer = _pointer_value(value, keep)
        if keep:
            owner._keep(addr - owner._b_addr if owner._b_addr else addr, keep[0])
        _write(addr, pointer.to_bytes(8, 'little'))
    elif code == 'P':
        keep = []
        try:
            pointer = _pointer_value(value, keep)
        except TypeError:
            raise TypeError('cannot be converted to pointer') from None
        if keep:
            owner._keep(addr - owner._b_addr if owner._b_addr else addr, keep[0])
        _write(addr, pointer.to_bytes(8, 'little'))
    elif code == 'O':
        owner._keep(addr - owner._b_addr, value)
        _write(addr, _lucifer.ct_object_address(value).to_bytes(8, 'little'))
    else:
        _write(addr, _encode(code, value, getattr(cls, '_swapped_', '')))


# ---------------------------------------------------------- struct / union

class CField:
    """A field of a Structure or Union: a descriptor that reads and writes the memory."""

    def __init__(self, name, ftype, offset, size, bit_size=None, bit_offset=0, index=0):
        self.name = name
        self.type = ftype
        self.offset = self.byte_offset = offset
        self.size = size
        self.byte_size = ftype._b_size_
        self.bit_size = bit_size
        self.bit_offset = bit_offset
        self.is_bitfield = bit_size is not None
        self.is_anonymous = False
        self._index = index

    def __repr__(self):
        return '<%s.%s %r type=%s, ofs=%d, size=%d>' % (
            type(self).__module__, type(self).__qualname__, self.name, self.type.__name__, self.offset, self.byte_size)

    def __get__(self, obj, objtype=None):
        if obj is None:
            return self
        addr = obj._b_addr + self.offset
        if self.is_bitfield:
            unit = int.from_bytes(_read(addr, self.byte_size), 'little')
            value = (unit >> self.bit_offset) & ((1 << self.bit_size) - 1)
            if self.type._b_code_ in 'bhilq' and value >> (self.bit_size - 1):
                value -= 1 << self.bit_size
            return value
        if 'value' in self.type.__dict__ and isinstance(self.type, PyCArrayType):      # char and wchar arrays
            return _at(self.type, obj._root(), addr).value
        return self.type._get(obj, addr)

    def __set__(self, obj, value):
        if value is NotImplemented:
            return
        addr = obj._b_addr + self.offset
        if self.is_bitfield:
            unit = int.from_bytes(_read(addr, self.byte_size), 'little')
            mask = ((1 << self.bit_size) - 1) << self.bit_offset
            unit = (unit & ~mask) | ((_index(value) << self.bit_offset) & mask)
            _write(addr, (unit & _MASK[self.type._b_code_]).to_bytes(self.byte_size, 'little'))
            return
        self.type._set(obj, addr, value)

    def __delete__(self, obj):
        raise TypeError('cannot delete attribute')


CField.__module__ = 'ctypes'


def _flat_fields(cls):
    """(name, CField) for every field a positional initializer fills, bases first."""
    out = []
    for base in reversed(cls.__mro__):
        for name, field in base.__dict__.get('_fields_by_order_', ()):
            out.append((name, field))
    return out


class _StructUnionType(_CDataType):
    _is_union = False

    def __new__(mcs, name, bases, ns, **kwargs):
        cls = super().__new__(mcs, name, bases, ns, **kwargs)
        base = next((b for b in cls.__mro__[1:] if isinstance(b, _StructUnionType) and hasattr(b, '_b_size_')), None)
        cls._b_size_ = base._b_size_ if base is not None else 0
        cls._b_align_ = base._b_align_ if base is not None else 1
        cls._finalized = False
        if '_fields_' in ns:
            cls._set_fields(ns['_fields_'])
        return cls

    def __setattr__(cls, name, value):
        if name == '_fields_':
            if cls.__dict__.get('_finalized'):
                raise AttributeError('_fields_ is final')
            cls._set_fields(value)
        super().__setattr__(name, value)

    def _set_fields(cls, fields):
        pack = getattr(cls, '_pack_', 0)
        base_size = 0
        for b in cls.__mro__[1:]:
            if isinstance(b, _StructUnionType) and b.__dict__.get('_finalized'):
                base_size = b._b_size_
                break
        offset = base_size if not cls._is_union else 0
        size = base_size if cls._is_union else 0
        align = max(cls._b_align_, 1)
        by_order = []
        unit = None                                  # the open storage unit of bit fields: (offset, ftype, used bits)
        for index, desc in enumerate(fields):
            if not isinstance(desc, tuple) or len(desc) not in (2, 3):
                raise TypeError('\'_fields_\' must be a sequence of (name, C type) pairs')
            name, ftype = desc[0], desc[1]
            if not isinstance(name, str):
                raise TypeError('\'_fields_\' must be a sequence of (name, C type) pairs')
            if not isinstance(ftype, _CDataType):
                raise TypeError('second item in _fields_ tuple (index %d) must be a C type' % index)
            fsize, falign = ftype._b_size_, ftype._b_align_
            if pack:
                falign = min(falign, pack)
            bits = desc[2] if len(desc) == 3 else None
            if bits is not None:
                if not isinstance(ftype, PyCSimpleType) or ftype._b_code_ not in _INTEGERS | {'?'}:
                    raise TypeError('bit fields not allowed for type %s' % ftype.__name__)
                if bits <= 0 or bits > fsize * 8:
                    raise ValueError('number of bits invalid for bit field %r' % name)
            if cls._is_union:
                field = CField(name, ftype, 0, fsize if bits is None else 0, bits, 0, index)
                size = max(size, fsize)
            elif bits is None:
                unit = None
                offset = _align_up(offset, falign)
                field = CField(name, ftype, offset, fsize, None, 0, index)
                offset += fsize
            else:
                if unit is None or unit[1] is not ftype and unit[1]._b_size_ != fsize or unit[2] + bits > fsize * 8:
                    offset = _align_up(offset, falign)
                    unit = [offset, ftype, 0]
                    offset += fsize
                field = CField(name, ftype, unit[0], 0, bits, unit[2], index)
                unit[2] += bits
            setattr_ = super(_StructUnionType, cls).__setattr__
            setattr_(name, field)
            by_order.append((name, field))
            align = max(align, falign)
        total = size if cls._is_union else offset
        type.__setattr__(cls, '_fields_by_order_', by_order)
        type.__setattr__(cls, '_b_size_', _align_up(total, align))
        type.__setattr__(cls, '_b_align_', align)
        type.__setattr__(cls, '_finalized', True)
        for name in getattr(cls, '_anonymous_', ()):
            inner = cls.__dict__.get(name)
            if not isinstance(inner, CField) or not isinstance(inner.type, _StructUnionType):
                raise AttributeError('Anonymous field %r must be a field of structure or union type' % name)
            inner.is_anonymous = True
            for sub_name, sub in _flat_fields(inner.type):
                combined = CField(sub_name, sub.type, inner.offset + sub.offset, sub.size, sub.bit_size,
                                  sub.bit_offset, sub._index)
                type.__setattr__(cls, sub_name, combined)

    def _get(cls, owner, addr):
        return _at(cls, owner._root(), addr)

    def _set(cls, owner, addr, value):
        if isinstance(value, cls):
            _write(addr, _read(value._b_addr, cls._b_size_))
            owner._keep('fields-%d' % addr, value) if value._objects else None
        elif isinstance(value, tuple):
            target = _at(cls, owner._root(), addr)
            target._init_fields(value, {})
        else:
            raise TypeError('expected %s instance, got %s' % (cls.__name__, type(value).__name__))

    def from_param(cls, value):
        if isinstance(value, cls):
            return value
        if hasattr(value, '_as_parameter_'):
            return cls.from_param(value._as_parameter_)
        raise TypeError('expected %s instance instead of %s' % (cls.__name__, type(value).__name__))


class PyCStructType(_StructUnionType):
    pass


class UnionType(_StructUnionType):
    _is_union = True


PyCStructType.__module__ = UnionType.__module__ = '_ctypes'
PyCStructType.__name__ = 'PyCStructType'
UnionType.__name__ = 'UnionType'


class _CStruct(_CData):
    def __init__(self, *args, **kwargs):
        self._init_fields(args, kwargs)

    def _init_fields(self, args, kwargs):
        fields = _flat_fields(type(self))
        if len(args) > len(fields):
            raise TypeError('too many initializers')
        for (name, _), value in zip(fields, args):
            setattr(self, name, value)
        for name, value in kwargs.items():
            setattr(self, name, value)

    def __repr__(self):
        return '<%s object at %#x>' % (type(self).__name__, id(self))


class Structure(_CStruct, metaclass=PyCStructType):
    """Structure base class"""


class Union(_CStruct, metaclass=UnionType):
    """Union base class"""


# ------------------------------------------------------------ simple fields

def _bind_get_set(meta):
    return meta


def _simple_value_get(cls, owner, addr):
    if cls.__bases__[0] is _SimpleCData or getattr(cls, '_swapped_', False) and cls.__bases__[0].__bases__[0] is _SimpleCData:
        return _simple_get(cls, owner._root(), addr)
    return _at(cls, owner._root(), addr)


def _simple_value_set(cls, owner, addr, value):
    if isinstance(value, cls):
        _write(addr, _read(value._b_addr, cls._b_size_))
        return
    _simple_set(cls, _holder(owner, addr), addr, value)


def _holder(owner, addr):
    """The object that keeps what a simple value written at addr refers to alive."""
    return owner


PyCSimpleType._get = _simple_value_get
PyCSimpleType._set = _simple_value_set


# ------------------------------------------------------------------ arrays

class PyCArrayType(_CDataType):
    def __new__(mcs, name, bases, ns, **kwargs):
        cls = super().__new__(mcs, name, bases, ns, **kwargs)
        if len(bases) == 1 and bases[0] is _CData or name == 'Array' and not hasattr(cls, '_type_'):
            return cls
        item = getattr(cls, '_type_', None)
        length = getattr(cls, '_length_', None)
        if item is None:
            raise AttributeError("class must define a '_type_' attribute")
        if length is None:
            raise AttributeError("class must define a '_length_' attribute")
        if not isinstance(length, int):
            raise TypeError("The '_length_' attribute must be an integer")
        if length < 0:
            raise ValueError("The '_length_' attribute must not be negative")
        if not isinstance(item, _CDataType):
            raise TypeError('_type_ must have storage info')
        cls._b_size_ = item._b_size_ * length
        cls._b_align_ = item._b_align_
        if isinstance(item, PyCSimpleType) and item._b_code_ in 'cu' and item.__bases__[0] is _SimpleCData:
            cls.value = _char_array_value(item._b_code_)
            if item._b_code_ == 'c':
                cls.raw = _char_array_raw
        return cls

    def _get(cls, owner, addr):
        return _at(cls, owner._root(), addr)

    def _set(cls, owner, addr, value):
        if isinstance(value, cls):
            _write(addr, _read(value._b_addr, cls._b_size_))
        elif isinstance(value, (tuple, list)):
            target = _at(cls, owner._root(), addr)
            target._assign(value)
        elif isinstance(value, (bytes, str)) and 'value' in cls.__dict__:      # c_char and c_wchar arrays
            _at(cls, owner._root(), addr).value = value
        else:
            raise TypeError('expected %s instance, got %s' % (cls.__name__, type(value).__name__))

    def from_param(cls, value):
        if isinstance(value, cls):
            return value
        if isinstance(value, (tuple, list)):
            return cls(*value)
        return _CDataType.from_param(cls, value)


PyCArrayType.__module__ = '_ctypes'


def _char_array_value(code):
    def get(self):
        data = _read(self._b_addr, self._b_size_)
        if code == 'c':
            end = data.find(b'\0')
            return data if end < 0 else data[:end]
        text = data.decode('utf-32-le', 'surrogatepass')
        end = text.find('\0')
        return text if end < 0 else text[:end]

    def set(self, value):
        if code == 'c':
            if not isinstance(value, bytes):
                raise TypeError('bytes expected instead of %s instance' % type(value).__name__)
            data = value
            limit = self._b_size_
        else:
            if not isinstance(value, str):
                raise TypeError('unicode string expected instead of %s instance' % type(value).__name__)
            data = value.encode('utf-32-le', 'surrogatepass')
            limit = self._b_size_
        if len(data) > limit:
            raise ValueError('byte string too long' if code == 'c' else 'string too long')
        _write(self._b_addr, data + (b'\0' * 4 if code == 'u' and len(data) < limit else b'\0' if len(data) < limit else b''))

    return property(get, set)


def _raw_get(self):
    return _read(self._b_addr, self._b_size_)


def _raw_set(self, value):
    data = bytes(memoryview(value))
    if len(data) > self._b_size_:
        raise ValueError('byte string too long')
    _write(self._b_addr, data)


_char_array_raw = property(_raw_get, _raw_set)


def _buffer_format(item):
    """(struct format of one element, shape suffix) for the buffer of an array of item."""
    if isinstance(item, PyCSimpleType):
        code = item._b_code_
        return {'l': 'l', 'L': 'L', 'u': 'i', 'z': 'P', 'Z': 'P', 'O': 'P', '?': '?', 'g': None}.get(code, code), ()
    if isinstance(item, PyCArrayType):
        fmt, shape = _buffer_format(item._type_)
        return fmt, (item._length_,) + shape
    return None, ()


class Array(_CData, metaclass=PyCArrayType):
    """Abstract base class for arrays."""

    def __init__(self, *args):
        self._assign(args)

    def _assign(self, values):
        if len(values) > self._length_:
            raise IndexError('invalid index')
        for i, value in enumerate(values):
            self[i] = value

    def __len__(self):
        return self._length_

    def _slice_values(self, indices):
        item = self._type_
        values = [self[i] for i in indices]
        if isinstance(item, PyCSimpleType) and item.__bases__[0] is _SimpleCData:
            if item._b_code_ == 'c':
                return b''.join(values)
            if item._b_code_ == 'u':
                return ''.join(values)
        return values

    def __getitem__(self, index):
        if isinstance(index, slice):
            return self._slice_values(range(*index.indices(self._length_)))
        index = _index(index)
        if not -self._length_ <= index < self._length_:
            raise IndexError('invalid index')
        index %= self._length_ or 1
        item = self._type_
        return item._get(self, self._b_addr + index * item._b_size_)

    def __setitem__(self, index, value):
        item = self._type_
        if isinstance(index, slice):
            indices = range(*index.indices(self._length_))
            values = list(value)
            if len(values) != len(indices):
                raise ValueError('Can only assign sequence of same size')
            for i, v in zip(indices, values):
                item._set(self, self._b_addr + i * item._b_size_, v)
            return
        index = _index(index)
        if not -self._length_ <= index < self._length_:
            raise IndexError('invalid index')
        item._set(self, self._b_addr + (index % self._length_) * item._b_size_, value)

    def __buffer__(self, flags, /):
        fmt, shape = _buffer_format(self._type_)
        raw = _lucifer.ct_view(self._b_addr, self._b_size_, False)
        if fmt is None or not self._b_size_:
            return raw
        return raw.cast('B').cast(fmt, (self._length_,) + shape)


# ---------------------------------------------------------------- pointers

class PyCPointerType(_CDataType):
    def __new__(mcs, name, bases, ns, **kwargs):
        cls = super().__new__(mcs, name, bases, ns, **kwargs)
        cls._b_size_ = cls._b_align_ = 8
        target = ns.get('_type_')
        if target is not None:
            if not isinstance(target, _CDataType):
                raise TypeError('_type_ must have storage info')
            if '__pointer_type__' not in target.__dict__:
                type.__setattr__(target, '__pointer_type__', cls)
        return cls

    def _get(cls, owner, addr):
        return _at(cls, owner._root(), addr)

    def _set(cls, owner, addr, value):
        if isinstance(value, cls):
            _write(addr, _read(value._b_addr, 8))
            return
        keep = []
        if isinstance(value, Array):
            if not issubclass(value._type_, cls._type_) and value._type_ is not cls._type_:
                raise TypeError('incompatible types, %s instance instead of %s instance'
                                % (type(value).__name__, cls.__name__))
        elif isinstance(value, _Pointer) and not isinstance(value, cls):
            raise TypeError('incompatible types, %s instance instead of %s instance'
                            % (type(value).__name__, cls.__name__))
        elif isinstance(value, _CData) and not isinstance(value, (Array, _Pointer, CFuncPtr)):
            raise TypeError('incompatible types, %s instance instead of %s instance'
                            % (type(value).__name__, cls.__name__))
        pointer = _pointer_value(value, keep)
        _write(addr, pointer.to_bytes(8, 'little'))
        owner._keep('ptr-%d' % (addr - owner._b_addr), value)

    def from_param(cls, value):
        if value is None:
            return cls()
        if isinstance(value, cls):
            return value
        if isinstance(value, Array) and issubclass(value._type_, cls._type_):
            return value
        if isinstance(value, CArgObject) and isinstance(value._obj, cls._type_):
            return value
        if hasattr(value, '_as_parameter_'):
            return cls.from_param(value._as_parameter_)
        raise TypeError('expected %s instead of %s' % (cls.__name__, type(value).__name__))

    def set_type(cls, item):
        type.__setattr__(cls, '_type_', item)


PyCPointerType.__module__ = '_ctypes'


class _Pointer(_CData, metaclass=PyCPointerType):
    """Pointer base class"""

    def __init__(self, value=None, /):
        if value is not None:
            self.contents = value

    @property
    def contents(self):
        """the object this pointer points to (read-write)"""
        address = int.from_bytes(_read(self._b_addr, 8), 'little')
        if address == 0:
            raise ValueError('NULL pointer access')
        return _at(self._type_, self._root(), address)

    @contents.setter
    def contents(self, value):
        if not isinstance(value, self._type_):
            raise TypeError('expected %s instead of %s' % (self._type_.__name__, type(value).__name__))
        _write(self._b_addr, value._b_addr.to_bytes(8, 'little'))
        self._keep('contents', value)

    def __bool__(self):
        return int.from_bytes(_read(self._b_addr, 8), 'little') != 0

    def _target(self, index):
        address = int.from_bytes(_read(self._b_addr, 8), 'little')
        if address == 0:
            raise ValueError('NULL pointer access')
        return address + index * self._type_._b_size_

    def __getitem__(self, index):
        item = self._type_
        if isinstance(index, slice):
            if index.stop is None:
                raise ValueError('slice stop is required')
            start, stop, step = index.start or 0, index.stop, index.step or 1
            values = [item._get(_Anchor(self), self._target(i)) for i in range(start, stop, step)]
            if isinstance(item, PyCSimpleType) and item.__bases__[0] is _SimpleCData:
                if item._b_code_ == 'c':
                    return b''.join(values)
                if item._b_code_ == 'u':
                    return ''.join(values)
            return values
        return item._get(_Anchor(self), self._target(_index(index)))

    def __setitem__(self, index, value):
        if isinstance(index, slice):
            raise TypeError('Pointer does not support item deletion' if value is None
                            else 'Slice assignment is not supported')
        if value is None and False:
            raise TypeError('Pointer does not support item deletion')
        self._type_._set(_Anchor(self), self._target(_index(index)), value)


class _Anchor:
    """The owner of memory a pointer points into: nobody, but the pointer keeps its target alive."""

    def __init__(self, pointer):
        self._pointer = pointer

    def _root(self):
        return self._pointer._root()

    def _keep(self, key, obj):
        self._pointer._keep(key, obj)

    @property
    def _b_addr(self):
        return 0


# ------------------------------------------------------------- functions

_UNSET = _Empty()


class PyCFuncPtrType(_CDataType):
    def __new__(mcs, name, bases, ns, **kwargs):
        cls = super().__new__(mcs, name, bases, ns, **kwargs)
        cls._b_size_ = cls._b_align_ = 8
        if name != 'CFuncPtr':
            flags = getattr(cls, '_flags_', None)
            if not isinstance(flags, int):
                raise TypeError('class must define _flags_ which must be an integer')
            argtypes = ns.get('_argtypes_')
            if argtypes is not None:
                for t in argtypes:
                    if not hasattr(t, 'from_param'):
                        raise TypeError('item %d in _argtypes_ has no from_param method' % (list(argtypes).index(t) + 1))
        return cls

    def _get(cls, owner, addr):
        return _at(cls, owner._root(), addr)

    def _set(cls, owner, addr, value):
        _write(addr, _read(value._b_addr, 8) if isinstance(value, CFuncPtr) else _pointer_value(value).to_bytes(8, 'little'))

    def from_param(cls, value):
        if value is None or isinstance(value, (cls, CFuncPtr)):
            return value if value is not None else cls()
        if isinstance(value, CArgObject):
            return value
        raise TypeError('expected %s instance instead of %s' % (cls.__name__, type(value).__name__))


PyCFuncPtrType.__module__ = '_ctypes'


def dlopen(name, mode=RTLD_LOCAL, /):
    """dlopen(name, flag={RTLD_GLOBAL|RTLD_LOCAL}) open a shared library"""
    return _lucifer.ct_dlopen(name, mode | 2)           # RTLD_NOW


def dlclose(handle, /):
    """dlclose a library"""
    _lucifer.ct_dlclose(handle)


def dlsym(handle, name, /):
    """find symbol in shared library"""
    address = _lucifer.ct_dlsym(handle, name)
    if isinstance(address, str):
        raise OSError(address)
    return address


def get_errno():
    return _lucifer.ct_errno(None)


def set_errno(value):
    return _lucifer.ct_errno(value)


# Python implementations behind the addresses ctypes asks for its own helpers.

def _cast_impl(pointer, obj, typ):
    if not isinstance(typ, (PyCPointerType, PyCSimpleType, PyCFuncPtrType)) or isinstance(typ, PyCSimpleType) \
            and typ._b_code_ not in 'zZP':
        raise TypeError('cast() argument 2 must be a pointer type, not %s' % (typ.__name__ if isinstance(typ, type) else type(typ).__name__))
    result = typ()
    _write(result._b_addr, (pointer or 0).to_bytes(8, 'little'))
    if isinstance(obj, _CData):
        result._keep('cast-source', obj)
    return result


def _string_at_impl(pointer, size):
    if pointer is None:
        raise ValueError('NULL pointer access')
    return _read(pointer, size if size >= 0 else _lucifer.ct_strlen(pointer, False))


def _wstring_at_impl(pointer, size):
    if pointer is None:
        raise ValueError('NULL pointer access')
    n = size if size >= 0 else _lucifer.ct_strlen(pointer, True)
    return _read(pointer, n * 4).decode('utf-32-le', 'surrogatepass')


def _memoryview_at_impl(pointer, size, readonly):
    return _lucifer.ct_view(pointer, size, readonly)


_PYNATIVE = {1: _cast_impl, 2: _string_at_impl, 3: _wstring_at_impl, 4: _memoryview_at_impl}
_cast_addr, _string_at_addr, _wstring_at_addr, _memoryview_at_addr = 1, 2, 3, 4
_libc = _lucifer.ct_dlopen(None, 2)
_memmove_addr = dlsym(_libc, 'memmove')
_memset_addr = dlsym(_libc, 'memset')


def _ffi_argument(param, index):
    """(libffi code, number or float) of a prepared argument."""
    if isinstance(param, CArgObject):
        return 'P', param._obj._b_addr + param.offset
    if isinstance(param, _SimpleCData):
        code = param._b_code_
        raw = _read(param._b_addr, param._b_size_)
        if code in 'fdg':
            return _FFI.get(code, code), _decode(code, raw)
        return _FFI.get(code, code), int.from_bytes(raw, 'little')
    if isinstance(param, (_Pointer, CFuncPtr)):
        return 'P', int.from_bytes(_read(param._b_addr, 8), 'little')
    if isinstance(param, Array):
        return 'P', param._b_addr
    if isinstance(param, (Structure, Union)):
        raise NotImplementedError('passing a structure by value to a foreign function')
    raise ArgumentError('argument %d: unsupported argument' % index)


def _default_param(value, index, keep):
    """The libffi argument for a value passed without a declared type."""
    if value is None:
        return 'P', 0
    if isinstance(value, bool):
        return 'i', int(value)
    if isinstance(value, int):
        if not -2**31 <= value < 2**32:
            raise ArgumentError('argument %d: int too long to convert' % index)
        return 'i', value
    if isinstance(value, float):
        return 'd', value
    if isinstance(value, (bytes, str)):
        return 'P', _pointer_value(value, keep)
    if hasattr(value, '_as_parameter_'):
        return _default_param(value._as_parameter_, index, keep)
    return _ffi_argument(value, index)


def _result_value(restype, number, code):
    """A Python value for a foreign call's raw result."""
    if restype is None:
        return None
    if isinstance(restype, PyCSimpleType):
        instance = restype.__new__(restype)
        if code in 'fd':
            _write(instance._b_addr, _encode(code, number))
        else:
            _write(instance._b_addr, (number & _MASK[restype._b_code_]).to_bytes(restype._b_size_, 'little')
                   if restype._b_code_ not in 'fdg' else _encode(restype._b_code_, number))
        return instance.__ctypes_from_outparam__()
    if isinstance(restype, (PyCPointerType, PyCFuncPtrType)):
        instance = restype()
        _write(instance._b_addr, (number or 0).to_bytes(8, 'little'))
        return instance
    return restype(number)


class CFuncPtr(_CData, metaclass=PyCFuncPtrType):
    """Function Pointer"""

    _restype_ = _UNSET
    _argtypes_ = None
    _flags_ = 0
    errcheck = None

    def __init__(self, *args):
        self._name = None
        self._callback = None
        self._address = 0
        if len(args) == 0:
            return
        target = args[0]
        if isinstance(target, int) and not isinstance(target, bool):
            self._address = target
        elif isinstance(target, tuple):
            name, dll = target
            handle = getattr(dll, '_handle', dll)
            if isinstance(name, int):
                raise AttributeError('function ordinal %d not found' % name)
            address = _lucifer.ct_dlsym(handle, name)
            if isinstance(address, str):
                raise AttributeError(address)
            self._address = address
            self._name = name
        elif callable(target):
            self._callback = target
            argtypes = tuple(self._argtypes_ or ())
            codes = tuple(_FFI.get(t._b_code_, t._b_code_) if isinstance(t, PyCSimpleType) else 'P' for t in argtypes)
            rtype = self.restype
            rcode = 'v' if rtype is None else _FFI.get(rtype._b_code_, rtype._b_code_) if isinstance(rtype, PyCSimpleType) else 'P'
            self._address = _lucifer.ct_callback(self._wrap_callback(target, argtypes, rtype), ord(rcode), tuple(ord(c) for c in codes))
        else:
            raise TypeError('argument must be callable or integer function address')
        _write(self._b_addr, self._address.to_bytes(8, 'little'))

    @staticmethod
    def _wrap_callback(function, argtypes, restype):
        def call(*numbers):
            args = [_result_value(t, n, _FFI.get(t._b_code_, t._b_code_) if isinstance(t, PyCSimpleType) else 'P')
                    for t, n in zip(argtypes, numbers)]
            result = function(*args)
            if restype is None or result is None:
                return 0
            return int(result.value if isinstance(result, _SimpleCData) else result)
        return call

    def __bool__(self):
        return int.from_bytes(_read(self._b_addr, 8), 'little') != 0

    @property
    def argtypes(self):
        return self._argtypes_

    @argtypes.setter
    def argtypes(self, value):
        if value is None:
            self._argtypes_ = None
            return
        value = tuple(value)
        for i, t in enumerate(value):
            if not hasattr(t, 'from_param'):
                raise TypeError('item %d in _argtypes_ has no from_param method' % (i + 1))
        self._argtypes_ = value

    @property
    def restype(self):
        return _c_int_default() if self._restype_ is _UNSET else self._restype_

    @restype.setter
    def restype(self, value):
        if value is not None and not isinstance(value, _CDataType) and not callable(value):
            raise TypeError('restype must be a type, a callable, or None')
        self._restype_ = value

    def __call__(self, *args):
        address = int.from_bytes(_read(self._b_addr, 8), 'little')
        if address == 0:
            raise ValueError('NULL function pointer')
        argtypes = self._argtypes_
        restype = self.restype
        keep = []
        params = []
        if argtypes is not None:
            if len(args) < len(argtypes):
                raise TypeError('this function takes at least %d argument%s (%d given)'
                                % (len(argtypes), '' if len(argtypes) == 1 else 's', len(args)))
            for i, (t, a) in enumerate(zip(argtypes, args)):
                try:
                    p = t.from_param(a)
                except Exception as e:
                    raise ArgumentError('argument %d: %s: %s' % (i + 1, type(e).__name__, e)) from None
                params.append(p)
            extra = args[len(argtypes):]
        else:
            extra = args
        native = _PYNATIVE.get(address)
        if native is not None:
            values = [p.value if isinstance(p, _SimpleCData) else p for p in params]
            return native(*values)
        flat = [_ffi_argument(p, i + 1) for i, p in enumerate(params)]
        flat += [_default_param(a, len(flat) + i + 1, keep) for i, a in enumerate(extra)]
        if isinstance(restype, PyCSimpleType):
            rcode = restype._b_code_
            rffi = _FFI.get(rcode, rcode)
        elif restype is None:
            rffi = 'v'
        elif isinstance(restype, (PyCPointerType, PyCFuncPtrType)):
            rffi = 'P'
        else:
            rffi = 'i'
        number = _lucifer.ct_call(address, ord(rffi), tuple(ord(c) for c, _ in flat), tuple(v for _, v in flat),
                                  self._flags_)
        result = _result_value(restype, number, rffi)
        if self.errcheck is not None:
            checked = self.errcheck(result, self, args)
            return checked
        return result

    def __repr__(self):
        return '<%s object at %#x>' % (type(self).__name__, id(self))


def _c_int_default():
    import ctypes
    return ctypes.c_int


CFuncPtr.__qualname__ = 'CFuncPtr'
