# A memoryview keeps its exporter alive through a managed buffer the collector
# must see: an exporter that holds its own view is garbage once unreferenced.
# Items accept __index__ objects like struct does; 'e' is IEEE half precision.
import gc
import struct
import weakref

gc.disable()
gc.collect()

freed = []
refs = []


class Exporter(bytearray):
    pass


def cycle(make_view):
    e = Exporter(b'abcd')
    e.view = make_view(e)
    refs.append(weakref.ref(e, lambda r: freed.append(1)))
    del e
    gc.collect()
    return len(freed)


print('cycle', cycle(memoryview))
print('slice cycle', cycle(lambda e: memoryview(e)[1:3]))
print('second view cycle', cycle(lambda e: (memoryview(e), memoryview(memoryview(e)))))
print('cast cycle', cycle(lambda e: memoryview(e).cast('B')))

e = Exporter(b'abcd')
mv = memoryview(e)
r = weakref.ref(e)
del e
gc.collect()
print('still held', r() is not None, bytes(mv))
mv.release()
gc.collect()
print('released', r())


class Idx:
    def __init__(self, v):
        self.v = v

    def __index__(self):
        return self.v


class BadIdx:
    def __index__(self):
        return 'x'


class Boom:
    def __index__(self):
        raise KeyError('boom')


class Fl:
    def __float__(self):
        return 1.5


def t(f):
    try:
        print(repr(f()))
    except BaseException as ex:
        print(type(ex).__name__, ex)


def setget(fmt, value):
    b = bytearray(16)
    m = memoryview(b).cast('B').cast(fmt)
    m[0] = value
    return m[0], bytes(b[:m.itemsize])


for fmt in 'bBhHiIlLqQnN':
    for v in (Idx(5), Idx(-1), Idx(1 << 70), Idx(-(1 << 70)), Idx(255), Idx(256), True, 1.5, '1', None, BadIdx(), Boom()):
        t(lambda: setget(fmt, v))
t(lambda: setget('?', Idx(0)))
t(lambda: setget('P', Idx(7)))
for v in (-1, 1 << 63, 1 << 64, -(1 << 63), -(1 << 63) - 1, True, 7):
    t(lambda: setget("P", v))
t(lambda: setget('f', Idx(3)))
t(lambda: setget('d', Idx(3)))
t(lambda: setget('d', Fl()))
t(lambda: setget('d', 'x'))

b = bytearray(4)
m = memoryview(b)
m[Idx(1)] = Idx(9)
t(lambda: bytes(b))
t(lambda: m.__setitem__(Idx(9), 1))

# The item sets another view's state: a release inside __index__ is an error.
class Releaser:
    def __index__(self):
        m2.release()
        return 1


m2 = memoryview(bytearray(4))
t(lambda: m2.__setitem__(0, Releaser()))

# half precision
vals = [0.0, -0.0, 1.0, -2.5, 0.1, 65504.0, 65520.0, 1e-8, 6e-8, 5.96e-8, 3e-5, float('inf'), float('-inf'),
        float('nan'), 1e5, -1e5, 2049.0, 1 / 3]
for v in vals:
    t(lambda: setget('e', v))
    t(lambda: struct.pack('<e', v))
t(lambda: setget('e', Idx(3)))
t(lambda: setget('e', Fl()))
t(lambda: setget('e', 'x'))
t(lambda: setget('e', 1e10))
src = bytes.fromhex('003c00bc00fcfe7b0001ff03')
h = memoryview(src).cast('e')
t(lambda: h.tolist())
t(lambda: h[3])
t(lambda: (h.format, h.itemsize, len(h)))
t(lambda: memoryview(bytearray(4)).cast('B').cast('e').tolist())
t(lambda: memoryview(b'\x00\x00\x01').cast('e'))
t(lambda: memoryview(src).cast('e').cast('B').tolist())
t(lambda: memoryview(struct.pack('<2e', 1.5, -3)).cast('e').tolist())
t(lambda: memoryview(src).cast('e') == memoryview(src).cast('e'))
