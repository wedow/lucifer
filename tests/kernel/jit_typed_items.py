# Hot reads and stores of bytearray items and of array items, for every
# typecode compiled code reads and stores in line (b B h H i I l L q Q f
# d): negative indexes, ints at each code's bounds, floats and ints stored
# into float arrays, sites that meet one typecode (each its own copy of
# the functions) and sites that meet them all; and the same sites given
# what they leave for: indexes out of range, ints out of a code's range,
# values of other types, a 'Q' item past 2**63, an index that is no int,
# and arrays of another typecode. An array exported to a memoryview takes
# stores the view sees and refuses to resize.
# Each result must be the interpreter's; tools/jitcheck also runs this at
# two sizes and requires that compiled code's helper calls do not grow
# with the iterations: the items of a site that meets one typecode are
# read and stored in line.
import sys
from array import array

N = int(sys.argv[1]) if len(sys.argv) > 1 else 3000

BOUNDS = {
    'b': (-128, 127), 'B': (0, 255), 'h': (-32768, 32767), 'H': (0, 65535),
    'i': (-2**31, 2**31 - 1), 'I': (0, 2**32 - 1), 'l': (1 - 2**63, 2**63 - 1), 'L': (0, 2**63 - 1),
    'q': (1 - 2**63, 2**63 - 1), 'Q': (0, 2**63 - 1),
}
# -2**63 is no machine int to compiled code: the sites given it below take the primitive's path.

SITES = """
def total(a, n):
    s = 0
    m = len(a)
    for i in range(n):
        s = ((s ^ a[i % m]) & 0xffffffff) + (a[-1 - i % m] & 255)
    return s


def ftotal(a, n):
    s = 0.0
    m = len(a)
    for i in range(n):
        s += a[i % m] * 0.5 + a[-1 - i % m]
    return s


def fill(a, vals, n):
    m, k = len(a), len(vals)
    for i in range(n):
        a[i % m] = vals[i % k]
        a[-1 - i % m] = vals[(i + 1) % k]
    return list(a)


def consts(a, n):
    m = len(a)
    for i in range(n):
        a[i % m] = 0.5
        a[-1 - i % m] = -1.25
        a[i % 2] = 7
    return list(a)
"""


def sites():
    """The functions above, compiled anew: sites of their own."""
    ns = {}
    exec(SITES, ns)
    return ns['total'], ns['ftotal'], ns['fill'], ns['consts']


shared = sites()
for code, (lo, hi) in BOUNDS.items():
    vals = [lo, hi, 0, 1, lo // 3, hi // 7]
    total, ftotal, fill, _ = sites()
    a = array(code, vals)
    print(code, total(a, N), fill(a, vals[::-1], N), total(a, N))
    print(code, shared[0](a, 300), shared[2](a, vals, 300))
for code in 'fd':
    total, ftotal, fill, consts = sites()
    a = array(code, [0.25, -1.5, 3.0, 1e10 if code == 'd' else 1024.0])
    print(code, ftotal(a, N), fill(a, [0.5, -2.0, 7.25, -3.0], N), ftotal(a, N), consts(a, N))
    print(code, shared[1](a, 300), shared[2](a, [0.5, -2.0, 7, -3], 300))
total, ftotal, fill, _ = sites()
b = bytearray(range(250, 256))
print('bytearray', total(b, N), fill(b, [0, 255, 7, 128], N), total(b, N))
print('bytearray', shared[0](b, 300), shared[2](b, [0, 255, 7, 128], 300))


def get(a, i):
    return a[i]


def put(a, i, v):
    a[i] = v


def attempt(fn, *args):
    try:
        r = fn(*args)
        return 'ok' if r is None else repr(r)
    except Exception as e:
        return '%s: %s' % (type(e).__name__, e)


# The hot sites, given what they leave for.
a = array('i', [1, 2, 3])
for _ in range(N):
    get(a, 1)
    put(a, 1, 2)
for args in ((a, 3), (a, -4), (a, 2**70), (a, -1), (a, True), (a, 1.0), (array('d', [1.5]), 0),
             (array('Q', [2**64 - 1]), 0), (array('w', 'xy'), 1), (array('b', [5, 6]), -2)):
    print('get', attempt(get, *args))
for args in ((b, 6), (b, -7), (b, -1), (b, 2**70), (b, 1.0), (b, True)):
    print('get', attempt(get, *args))
for args in ((b, 0, 256), (b, 0, -1), (b, 6, 1), (b, -7, 1), (b, 0, 'x'), (b, 0, 1.5), (b, 1.0, 1), (b, -1, 9),
             (b, 0, True), (b, 0, 2**70)):
    print('put', attempt(put, *args), list(b))
for args in ((a, 3, 1), (a, -4, 1), (a, 0, 2**31), (a, 0, -2**31 - 1), (a, 0, 1.0), (a, 0, 'x'), (a, 0, True),
             (a, 0, 2**70), (a, -1, 9), (a, 1.0, 1), (a, 'x', 1), (array('d', [0.0]), 0, 2**60 + 1),
             (array('d', [0.0]), 0, 2**1024),
             (array('f', [0.0]), 0, 1e300), (array('B', [0]), 0, 256), (array('Q', [0]), 0, 2**64 - 1),
             (array('L', [0]), 0, -1), (array('w', 'xy'), 0, 'z'), (array('q', [0]), 0, -2**63),
             (array('l', [0]), -1, -2**63 - 1)):
    print('put', attempt(put, *args), args[0].tolist())


def refused(fn, *args):
    try:
        fn(*args)
        return 'ok'
    except Exception as e:
        return type(e).__name__


# Exported: stores reach the view, resizes are refused.
a = array('d', [1.0, 2.0, 3.0])
m = memoryview(a)
print(sites()[2](a, [4.5, 5.5, 6.5, 7.5], N), bytes(m) == a.tobytes(), refused(a.append, 4.0), refused(a.pop))
m.release()
a.append(4.0)
print(a.tolist(), sites()[1](a, N))
