import ctypes
from ctypes import (Structure, Union, POINTER, pointer, byref, sizeof, alignment, cast, addressof, c_int, c_uint, c_long,
                    c_short, c_ubyte, c_double, c_float, c_char, c_char_p, c_wchar, c_wchar_p, c_void_p, c_bool, c_size_t,
                    c_longlong, c_int8, c_uint64, py_object, string_at, memmove, memset, CFUNCTYPE, create_string_buffer, Array)


def show(f, *args):
    try:
        print(repr(f(*args)))
    except BaseException as e:
        print(type(e).__name__, e)


# sizes
print([sizeof(t) for t in (c_int, c_uint, c_long, c_short, c_ubyte, c_double, c_float, c_char, c_wchar, c_void_p, c_bool,
                           c_size_t, c_longlong, c_int8, c_uint64, c_char_p)])
print(alignment(c_int), alignment(c_double), alignment(c_char), sizeof(c_int * 5), sizeof(POINTER(c_int)))
print(c_int.__name__, type(c_int).__name__, c_int.__mro__[1].__name__, c_int._type_, ctypes.c_longdouble._type_)

# simple values
print(c_int(5), c_int(5).value, c_int(2**31).value, c_int(-1).value, c_uint(-1).value, c_ubyte(257).value, c_short(70000).value)
print(c_double(1.5).value, c_float(0.1).value, c_bool(5).value, c_bool().value, c_char(b'x'), c_char(65).value, c_wchar('é').value)
show(c_int, 1.5)
show(c_int, 'x')
show(c_char, b'xy')
show(c_double, 'x')
show(c_wchar, 'ab')
i = c_int()
i.value = 77
print(i.value, bool(i), bool(c_int()), repr(c_double(0.25)), type(c_char_p(b'hi')).__name__, repr(c_void_p()), repr(c_void_p(0x1234)))
print(c_char_p(b'text').value, c_char_p(None).value, c_wchar_p('wide').value, c_void_p(None).value, c_void_p(7).value)
show(c_char_p, 5.5)
show(c_char_p, 'unicode')
class MyInt(c_int):
    pass
m = MyInt(3)
print(m.value, isinstance(m, c_int), repr(m).startswith('<MyInt object at'))
obj = py_object([1, 2])
print(obj.value, repr(obj))
show(lambda: py_object().value)

# arrays
arr = (c_int * 4)(1, 2, 3)
print(len(arr), list(arr), arr[0], arr[-1], arr[1:3], arr[::2], sizeof(arr), arr._length_, arr._type_.__name__, type(arr).__name__)
arr[3] = 40
arr[0:2] = [7, 8]
print(list(arr), sum(arr))
show(lambda: arr[4])
show(lambda: arr.__setitem__(4, 1))
show(lambda: (c_int * 2)(1, 2, 3))
show(lambda: c_int * -1)
buf = create_string_buffer(b'abc', 8)
print(buf.value, buf.raw, len(buf), sizeof(buf), buf[0], buf[:2])
buf.value = b'xyz'
print(buf.value, buf.raw[:5])
show(lambda: setattr(buf, 'value', b'0123456789'))
wbuf = (c_wchar * 4)('a', 'b')
print(wbuf.value, len(wbuf), wbuf[:])
print(memoryview(arr).tobytes().hex(), memoryview(arr).shape, memoryview(arr).itemsize, memoryview((c_double * 2)(1.5, 2.5)).nbytes)
grid = (c_int * 3 * 2)()
grid[1][2] = 9
print([list(row) for row in grid], sizeof(grid), memoryview(grid).shape)

# structures and unions
class Point(Structure):
    _fields_ = [('x', c_int), ('y', c_int)]

class Mixed(Structure):
    _fields_ = [('tag', c_char), ('n', c_long), ('d', c_double), ('s', c_short)]

class Flags(Structure):
    _fields_ = [('a', c_uint, 3), ('b', c_uint, 5), ('c', c_uint, 1), ('full', c_int)]

class Number(Union):
    _fields_ = [('i', c_int), ('f', c_float), ('b', c_ubyte * 4)]

class Outer(Structure):
    _fields_ = [('p', Point), ('arr', c_int * 2), ('name', c_char * 6), ('next', POINTER(Point))]

p = Point(1, 2)
print(p.x, p.y, sizeof(Point), alignment(Point), sizeof(Mixed), alignment(Mixed), sizeof(Flags), sizeof(Number), sizeof(Outer))
print([(f, getattr(Mixed, f).offset, getattr(Mixed, f).size) for f in 'tag n d s'.split()])
p.x = 10
print(p.x, p.y, Point(y=5).x, Point(y=5).y)
show(Point, 1, 2, 3)
show(lambda: setattr(p, 'x', 'bad'))
show(lambda: getattr(p, 'z'))
f = Flags(a=5, b=17, c=1, full=-3)
print(f.a, f.b, f.c, f.full, bytes(memoryview(f))[:4].hex())
f.a = 9
print(f.a, f.b)
n = Number(i=0x3f800000)
print(n.f, list(n.b), n.i)
o = Outer()
o.p.x = 4
o.arr[1] = 8
o.name = b'hello'
print(o.p.x, o.p.y, list(o.arr), o.name, bool(o.next), type(o.p).__name__, o.p is o.p)
o.next = pointer(p)
print(o.next.contents.x, o.next[0].y, bool(o.next))
print(Point.x, Point.x.offset, Point.y.size, Mixed.d.byte_offset)
class Sub(Point):
    _fields_ = [('z', c_int)]
print(sizeof(Sub), Sub(1, 2, 3).z, Sub(1, 2, 3).y)
class Anon(Structure):
    _anonymous_ = ('u',)
    _fields_ = [('kind', c_int), ('u', Number)]
a = Anon(kind=1)
a.i = 5
print(a.i, a.u.i, sizeof(Anon))
class Packed(Structure):
    _pack_ = 1
    _layout_ = 'ms'
    _fields_ = [('a', c_char), ('b', c_int)]
print(sizeof(Packed), Packed.b.offset)
class Late(Structure):
    pass
Late._fields_ = [('v', c_int), ('self', POINTER(Late))]
print(sizeof(Late), Late.self.offset)
show(setattr, Late, '_fields_', [])

# pointers
x = c_int(42)
px = pointer(x)
print(px.contents.value, px[0], type(px).__name__, px._type_.__name__, POINTER(c_int) is POINTER(c_int), bool(px), bool(POINTER(c_int)()))
px[0] = 43
px.contents.value += 1
print(x.value, addressof(x) == addressof(px.contents), cast(px, c_void_p).value == addressof(x))
show(lambda: POINTER(c_int)().contents)
parr = (c_int * 3)(5, 6, 7)
pp = cast(parr, POINTER(c_int))
print(pp[0], pp[2], pp[1:3], [pp[i] for i in range(3)])
pp[1] = 60
print(list(parr))
pl = cast(byref(x), POINTER(c_int))
print(pl[0])
show(cast, 5, c_int)
print(string_at(b'hello world', 5), string_at(create_string_buffer(b'abc')), type(byref(x)).__name__)
show(byref, 5)

# memory
src = (c_ubyte * 4)(1, 2, 3, 4)
dst = (c_ubyte * 4)()
memmove(dst, src, 3)
print(list(dst))
memset(dst, 255, 2)
print(list(dst))
raw = bytearray(b'\x01\x00\x00\x00\x02\x00\x00\x00')
view = (c_int * 2).from_buffer(raw)
view[1] = 5
print(list(view), raw.hex(), (c_int * 2).from_buffer_copy(b'\x03\x00\x00\x00\x04\x00\x00\x00')[:])
show((c_int * 4).from_buffer, bytearray(4))
show(c_int.from_buffer, b'\0\0\0\0')
blob = (c_int * 2)(11, 22)
print(c_int.from_address(addressof(blob) + 4).value)

# libc
libc = ctypes.CDLL(None)
libc.strlen.argtypes = [c_char_p]
libc.strlen.restype = c_size_t
print(libc.strlen(b'hello'), libc.abs(-5), libc.atoi(b'123'))
libc.sqrt if False else None
libm = ctypes.CDLL('libm.so.6')
libm.pow.restype = c_double
libm.pow.argtypes = [c_double, c_double]
print(libm.pow(2.0, 10.0), libm.floor(c_double(1.5)) if False else '')
try:
    libc.no_such_function_xyz
except AttributeError as e:
    print('AttributeError', str(e).rpartition(': ')[2])
show(libc.strlen, 5)
show(libc.strlen)
print(libc.printf if False else 'ok')

Compare = CFUNCTYPE(c_int, POINTER(c_int), POINTER(c_int))
def compare(a, b):
    return a[0] - b[0]
values = (c_int * 5)(5, 1, 4, 2, 3)
libc.qsort(values, len(values), sizeof(c_int), Compare(compare))
print(list(values))
print(Compare.__name__ if False else sizeof(Compare), ctypes.get_errno())
ctypes.set_errno(5)
print(ctypes.get_errno())
