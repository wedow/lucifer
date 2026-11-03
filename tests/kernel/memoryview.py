# memoryview over bytes and bytearray: slicing, casts, assignment, release, equality.
def attempt(f):
    try:
        print(repr(f()))
    except Exception as e:
        print(type(e).__name__, e)


m = memoryview(b'abcdefgh')
print(m.format, m.itemsize, m.ndim, m.shape, m.strides, m.nbytes, m.readonly, m.suboffsets, m.c_contiguous, m.f_contiguous, m.contiguous)
print(m.obj, m.tobytes(), m.tolist(), len(m), m[0], m[-1], m[2:5].tobytes(), m[::2].tobytes(), m[::-1].tobytes(), m[5:2].tobytes())
print(m[2:6][1:3].tobytes(), m[::2][1:].tobytes(), m[::2].c_contiguous, m[::2].strides, m[::2].shape, m[::-3].tolist())
print(m.hex(), m.hex(':'), m[1:3].hex('-', 1))
print(m == b'abcdefgh', b'abcdefgh' == m, m == memoryview(b'abcdefgh'), m == m[:], m != b'x', m == 1, m[:3] == b'abc', m[::2] == b'aceg')
print(hash(m) == hash(b'abcdefgh'), hash(m[1:3]) == hash(b'bc'))
print([x for x in m], m.count(97), m.index(99), m.index(99, 1, 4))
w = bytearray(b'abcdefgh')
mw = memoryview(w)
print(mw.readonly, mw.obj is w)
mw[0] = 65
mw[-1] = 72
mw[2:4] = b'XY'
mw[::2] = b'1234'
print(w)
mw[::-1] = mw[::-1].tobytes()
print(w)
attempt(lambda: mw.__setitem__(0, 256))
attempt(lambda: mw.__setitem__(0, 'a'))
attempt(lambda: mw.__setitem__(20, 1))
attempt(lambda: mw.__setitem__(slice(0, 2), b'abc'))
attempt(lambda: mw.__setitem__(slice(0, 2), 5))
attempt(lambda: m.__setitem__(0, 1))
attempt(lambda: m.__delitem__(0))
attempt(lambda: m[20])
attempt(lambda: m['a'])
attempt(lambda: m[1.5])
attempt(lambda: hash(mw))
attempt(lambda: memoryview('str'))
attempt(lambda: memoryview(5))
attempt(lambda: memoryview())
attempt(lambda: m.tobytes('x'))
print(mw.toreadonly().readonly, m.toreadonly().tobytes())
ro = mw.toreadonly()
attempt(lambda: ro.__setitem__(0, 1))

# casts
c = memoryview(bytes(range(16)))
h = c.cast('H')
print(h.format, h.itemsize, h.shape, h.strides, h.nbytes, h.tolist(), h[1], len(h), h.cast('B').tolist() == c.tolist())
i = c.cast('I')
print(i.tolist(), c.cast('Q').tolist(), c.cast('b').tolist()[:3], c.cast('c').tolist()[:2], c.cast('?').tolist()[:2])
n = c.cast('B', (4, 4))
print(n.shape, n.strides, n.ndim, n.tolist(), n.tobytes() == bytes(range(16)), n.cast('B').shape)
print(c.cast('h', [2, 4]).tolist(), c.cast('d').format, c.cast('f').itemsize)
attempt(lambda: c.cast('H').cast('I'))
attempt(lambda: c[1:4].cast('H'))
attempt(lambda: c.cast('x'))
attempt(lambda: c.cast('B', (3, 3)))
attempt(lambda: c.cast('B', 5))
attempt(lambda: n[1])
attempt(lambda: n[1:2].shape)
attempt(lambda: c[::2].cast('H'))
fm = memoryview(bytearray(8)).cast('H')
fm[0] = 65535
fm[1] = 258
attempt(lambda: fm.__setitem__(0, 65536))
attempt(lambda: fm.__setitem__(0, -1))
attempt(lambda: fm.__setitem__(0, 1.5))
print(fm.tolist(), fm.tobytes())
sg = memoryview(bytearray(8)).cast('b')
sg[0] = -128
sg[1] = 127
attempt(lambda: sg.__setitem__(2, 128))
print(sg.tolist())
dd = memoryview(bytearray(16)).cast('d')
dd[0] = 1.5
dd[1] = 3
print(dd.tobytes(), dd == dd)
u64 = memoryview(bytearray(8)).cast('Q')
u64[0] = 2 ** 64 - 1
print(u64.tolist(), u64[0] == 2 ** 64 - 1)
attempt(lambda: u64.__setitem__(0, 2 ** 64))
ch = memoryview(bytearray(b'abc')).cast('c')
ch[0] = b'z'
print(ch.tolist(), ch.format)
attempt(lambda: ch.__setitem__(0, b'zz'))

# release and exports
r = memoryview(bytearray(b'xyz'))
r2 = r[1:]
r.release()
r.release()
attempt(lambda: r.tobytes())
attempt(lambda: len(r))
attempt(lambda: r[0])
attempt(lambda: r.format)
attempt(lambda: r.obj)
print(r2.tobytes(), repr(r).startswith('<released memory at 0x'), repr(r2).startswith('<memory at 0x'))
with memoryview(b'ctx') as cm:
    print(cm.tobytes())
attempt(lambda: cm.tobytes())
attempt(lambda: memoryview(r))
print(memoryview(r2).tobytes(), memoryview(r2) == r2)
nested = memoryview(m)
print(nested.tobytes(), nested.obj is m.obj)
x = bytearray(b'12')
mx = memoryview(x)
bm = bytes(mx)
print(bm, bytearray(mx), b'0' + mx.tobytes())
class Exporter:
    pass
attempt(lambda: memoryview(Exporter()))
print(type(m).__name__, type(m[0]).__name__, memoryview.__name__)
