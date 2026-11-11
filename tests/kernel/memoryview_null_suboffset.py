# Iterating a PIL-style memoryview (suboffsets) whose pointer table holds NULL:
# the iterator stops at that item and goes on with the next.
import ctypes as ct


class Buf(ct.Structure):
    _fields_ = [("buf", ct.c_void_p), ("obj", ct.c_void_p), ("len", ct.c_ssize_t),
                ("itemsize", ct.c_ssize_t), ("readonly", ct.c_int), ("ndim", ct.c_int),
                ("format", ct.c_char_p), ("shape", ct.c_void_p), ("strides", ct.c_void_p),
                ("suboffsets", ct.c_void_p), ("internal", ct.c_void_p)]


data = (ct.c_char * 3).from_buffer_copy(bytes([3, 0, 2]))
table = (ct.c_void_p * 3)(None, ct.addressof(data) + 1, ct.addressof(data) + 2)
shape = (ct.c_ssize_t * 1)(3)
strides = (ct.c_ssize_t * 1)(8)
suboffsets = (ct.c_ssize_t * 1)(0)
fmt = ct.create_string_buffer(b"B")
info = Buf(ct.addressof(table), None, 3, 1, 1, 1, ct.cast(fmt, ct.c_char_p),
           ct.addressof(shape), ct.addressof(strides), ct.addressof(suboffsets), None)
f = ct.pythonapi.PyMemoryView_FromBuffer
f.restype = ct.py_object
f.argtypes = [ct.POINTER(Buf)]
m = f(ct.byref(info))
it = iter(m)
print(type(it).__name__)
for _ in range(4):
    try:
        print(next(it))
    except StopIteration:
        print("stop")
print(m.count(3), m.count(0), m.count(2))   # count iterates: it stops at the NULL item
table[0] = ct.addressof(data)
print(m.count(3), m.count(0), m.index(2), m.index(0, -2), m.index(3, -9, 9))
print(list(m), m[1], list(iter(m)))

# The iterator over plain views.
m = memoryview(b'abc')
it = iter(m)
print(next(it), list(it), list(it), it.__length_hint__() if hasattr(it, '__length_hint__') else '-')
print(list(memoryview(bytearray(b'\1\2\3\4')).cast('H')))
for bad in (lambda: memoryview(b'a').cast('B', ()),
            lambda: memoryview(b'abcd').cast('B', (2, 2))):
    try:
        iter(bad())
    except Exception as e:
        print(type(e).__name__, e)
m = memoryview(b'abcab')
for f in (lambda: m.index(98, 2), lambda: m.index(98, 2, 3), lambda: m.index(97, 10**30),
          lambda: memoryview(b'a').cast('B', ()).index(1), lambda: memoryview(b'a').cast('B', ()).count(1),
          lambda: memoryview(b'abcd').cast('B', (2, 2)).index(1),
          lambda: memoryview(b'abcd').cast('B', (2, 2)).count(1)):
    try:
        print(f())
    except Exception as e:
        print(type(e).__name__, e)
r = memoryview(b'xy')
it = iter(r)
r.release()
try:
    next(it)
except Exception as e:
    print(type(e).__name__, e)
