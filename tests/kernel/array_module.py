import array
import copy
import pickle
import warnings

warnings.simplefilter('ignore')


def show(f):
    try:
        return repr(f())
    except Exception as e:
        return '%s: %s' % (type(e).__name__, e)


for tc, vals in (('b', (-129, 128)), ('B', (-1, 256)), ('h', (-32769, 32768)), ('H', (-1, 65536)),
                 ('i', (-2**31 - 1, 2**31)), ('I', (-1, 2**32)), ('l', (-2**63 - 1, 2**63)), ('L', (-1, 2**64)),
                 ('q', (-2**63 - 1, 2**63)), ('Q', (-1, 2**64)), ('u', (5,)), ('w', (5,))):
    for v in vals:
        print(tc, v, show(lambda: array.array(tc, [v])))
a = array.array('i', [1, 2, 3])
print(repr(a), a.itemsize, array.typecodes, len(a), a.typecode, list(a), a[1], a[-1], a[::2], a[::-1], 2 in a)
for f in (lambda: array.array('z'), lambda: array.array('i', [1.5]), lambda: array.array('i', 'x'), lambda: array.array('d', ['x']),
          lambda: a.pop(10), lambda: array.array('i').pop(), lambda: a.index(9), lambda: a.remove(9), lambda: a.frombytes(b'123'),
          lambda: a[5], lambda: a.tounicode(), lambda: a.fromlist(5), lambda: a.extend([1.5]), lambda: a + array.array('l'),
          lambda: array.array(), lambda: array.array('i', [1], [2]), lambda: array.array('u', 'ab').append(3)):
    print(show(f))
print(repr(array.array('u', 'héllo')), repr(array.array('d', [1.5, 2])), repr(array.array('i')), repr(array.array('B', b'abc')))
b = array.array('h', [5, -6, 7])
b.append(8)
b.extend([9, 10])
b.extend(array.array('h', [11]))
b.insert(1, 100)
b.reverse()
print(b, b.pop(), b.pop(0), b.count(7), b.index(7), b.tobytes(), b.tolist(), b + b, b * 2, 2 * b, b == b[:], b < b + b)
b.remove(100)
b[0] = 42
b[1:3] = array.array('h', [1, 2, 3])
del b[0]
b.byteswap()
print(b, b.buffer_info()[1], bytes(b), bytearray(b), b'x' + bytes(b))
b *= 2
b += array.array('h', [1])
print(len(b), b.tobytes().hex())
print(copy.copy(b) == b, copy.deepcopy(b) == b, pickle.loads(pickle.dumps(b)) == b, pickle.loads(pickle.dumps(b, 2)) == b,
      pickle.loads(pickle.dumps(array.array('d', [1.5]), 0)), array.array('i', [1, 2]).__reduce_ex__(2))
f = array.array('f', [1.5, 2.25, 0.1])
print(f, array.array('d', f), array.array('b', [1, 2]) == array.array('i', [1, 2]), array.array('u', 'ab').tounicode())
u = array.array('w', 'xyz')
u.fromunicode('!')
print(u, u.tounicode(), u.itemsize)
import io
s = io.BytesIO()
b.tofile(s)
c = array.array('h')
c.fromfile(io.BytesIO(s.getvalue()), 3)
print(c, show(lambda: c.fromfile(io.BytesIO(b'ab'), 2)), c)
print(array.array[int], array.array('i', [3, 1, 2]).__class__.__name__)
