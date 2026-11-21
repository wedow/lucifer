# Call the C API directly. Check whether the result shares the export or copies it,
# as well as the layout and contents of copies.
import ctypes as ct

get_contiguous = ct.pythonapi.PyMemoryView_GetContiguous
get_contiguous.argtypes = [ct.py_object, ct.c_int, ct.c_char]
get_contiguous.restype = ct.py_object
READ, WRITE = 0x100, 0x200


def describe(view):
    print(view.format, view.itemsize, view.shape, view.strides, view.suboffsets,
          view.readonly, view.c_contiguous, view.f_contiguous, view.tolist())


def report_error(obj, mode, order):
    try:
        get_contiguous(obj, mode, order)
    except (BufferError, TypeError, ValueError) as exc:
        print(type(exc).__name__, str(exc))
    else:
        print('unexpected success')


data = bytearray(b'abcdef')
source = memoryview(data)[1:5]
alias = get_contiguous(source, READ, b'A')
writable = get_contiguous(data, WRITE, b'F')
print(alias is source, alias.obj is data, writable.obj is data)
source.release()
writable[1] = ord('X')
print(alias.tobytes(), data)
writable.release()
alias.release()
data.extend(b'g')
print(data)

source = memoryview(data)[::-2]
copy = get_contiguous(source, READ, b'C')
report_error(source, WRITE, b'C')
source.release()
data[0] = 0
print(type(copy.obj).__name__, copy.obj)
describe(copy)

report_error(b'abc', WRITE, b'C')
report_error(memoryview(b'abc')[::2], WRITE, b'C')
report_error(source, READ, b'A')
report_error(123, READ, b'C')


class Buffer(ct.Structure):
    _fields_ = [("buf", ct.c_void_p), ("obj", ct.c_void_p), ("len", ct.c_ssize_t),
                ("itemsize", ct.c_ssize_t), ("readonly", ct.c_int), ("ndim", ct.c_int),
                ("format", ct.c_void_p), ("shape", ct.c_void_p), ("strides", ct.c_void_p),
                ("suboffsets", ct.c_void_p), ("internal", ct.c_void_p)]


from_buffer = ct.pythonapi.PyMemoryView_FromBuffer
from_buffer.argtypes = [ct.POINTER(Buffer)]
from_buffer.restype = ct.py_object

# Both layouts require a copy. Include a negative stride and a format whose
# storage belongs to the source. The F result should share its export for A.
words = (ct.c_ushort * 8)(1, 0, 2, 0, 3, 0, 4, 0)
fmt = ct.create_string_buffer(b'H')
shape = (ct.c_ssize_t * 2)(2, 2)
strides = (ct.c_ssize_t * 2)(8, -4)
source = from_buffer(ct.byref(Buffer(ct.addressof(words) + 4, None, 8, 2, 0, 2,
                                    ct.addressof(fmt), ct.addressof(shape),
                                    ct.addressof(strides), None, None)))
copies = [get_contiguous(source, READ, order) for order in (b'C', b'F', b'A')]
source.release()
fmt.value = b'h'
words[0] = 100
for copy in copies:
    describe(copy)
    print(memoryview(copy.obj).cast('H').tolist())
f_copy = copies[1]
f_alias = get_contiguous(f_copy, READ, b'A')
c_copy = get_contiguous(f_copy, READ, b'C')
print(f_alias.obj is f_copy.obj, c_copy.obj is f_copy.obj)
f_copy.release()
describe(f_alias)
describe(c_copy)

# Indirect exporters require following the pointer table before copying.
data = ct.create_string_buffer(b'abc')
table = (ct.c_void_p * 2)(ct.addressof(data) + 1, ct.addressof(data))
shape = (ct.c_ssize_t * 1)(2)
strides = (ct.c_ssize_t * 1)(ct.sizeof(ct.c_void_p))
suboffsets = (ct.c_ssize_t * 1)(1)
fmt = ct.create_string_buffer(b'B')
source = from_buffer(ct.byref(Buffer(ct.addressof(table), None, 2, 1, 0, 1,
                                    ct.addressof(fmt), ct.addressof(shape),
                                    ct.addressof(strides), ct.addressof(suboffsets), None)))
report_error(source, WRITE, b'A')
describe(get_contiguous(source, READ, b'A'))

shape = (ct.c_ssize_t * 2)(2, 0)
strides = (ct.c_ssize_t * 2)(ct.sizeof(ct.c_void_p), 1)
suboffsets = (ct.c_ssize_t * 2)(0, -1)
empty = from_buffer(ct.byref(Buffer(ct.addressof(table), None, 0, 1, 0, 2,
                                   ct.addressof(fmt), ct.addressof(shape),
                                   ct.addressof(strides), ct.addressof(suboffsets), None)))
for order in (b'C', b'F'):
    describe(get_contiguous(empty, READ, order))

scalar = memoryview(b'x').cast('B', ())
describe(get_contiguous(scalar, READ, b'F'))
