"""bytearray slice stores from bytes, bytearrays and memoryviews (growing,
shrinking, negative and clamped bounds, the bytearray itself, an empty
slice past the end, a view whose export blocks the resize), memoryview()
with one argument and its errors, and a view's attributes read in a hot
loop, before and after release."""
from support import loops


def stores():
    out = []
    ba = bytearray(b'0123456789')
    ba[2:4] = b'ab'
    ba[0:0] = bytearray(b'<<')
    ba[-3:] = memoryview(b'XYZW')
    ba[100:200] = b'!'
    ba[5:2] = b'^'
    ba[1:3] = b''
    out.append(bytes(ba))
    ba[:] = ba
    out.append(bytes(ba))
    ba[0:2] = memoryview(bytearray(b'mv'))[0:1]
    out.append(bytes(ba))
    view = memoryview(ba)
    try:
        ba[0:1] = b'longer'
    except BufferError as e:
        out.append(('BufferError', str(e)))
    ba[0:1] = b'='
    view.release()
    out.append(bytes(ba))
    for bad in (3, 'str', [1, 2]):
        try:
            memoryview(bad)
        except TypeError as e:
            out.append(str(e))
    return out


def views(n):
    s = 0
    buf = bytearray()
    for i in range(n):
        with memoryview(b'abcdef') as v:
            s += v.nbytes + v.itemsize + (1 if v.readonly else 0)
            buf[len(buf):] = v[1:3]
    v = memoryview(b'xy')
    v.release()
    try:
        v.nbytes
    except ValueError as e:
        s += len(str(e))
    return s, bytes(buf[:6]), len(buf)


def run():
    return stores(), views(300)


for _ in range(loops()):
    RESULT = run()
print(repr(RESULT))
