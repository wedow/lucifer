"""The oracle's _bisect, _heapq and _statistics, unmodified, called every way
an argument-clinic function can be: positional, keyword, wrong type, wrong count."""
import _bisect, _heapq, _statistics, os, sys


def show(label, fn, *args, **kw):
    try:
        print(label, repr(fn(*args, **kw)))
    except BaseException as e:
        print(label, type(e).__name__, e)


for m in (_bisect, _heapq, _statistics):
    print(m.__name__, type(m).__name__, m.__spec__.loader.__class__.__name__,
          os.path.basename(m.__file__).split('.')[0], m.__doc__ is not None,
          sorted(n for n in dir(m) if not n.startswith('__')))
    print([(n, type(getattr(m, n)).__name__, getattr(m, n).__module__) for n in sorted(dir(m))
           if callable(getattr(m, n))])
print(_heapq.__about__[:40])
print(sys.modules['_heapq'] is _heapq)

a = [1, 3, 5, 7]
show('right', _bisect.bisect_right, a, 4)
show('left', _bisect.bisect_left, a, 5)
show('lohi', _bisect.bisect_left, a, 5, 1, 3)
show('hi None', _bisect.bisect_left, a, 5, 0, None)
show('kw', _bisect.bisect_right, a, 4, lo=1, hi=2)
show('key', _bisect.bisect_right, ['a', 'bb', 'ccc'], 2, key=len)
show('lo neg', _bisect.bisect_left, a, 5, -1)
show('lo type', _bisect.bisect_left, a, 5, 'x')
show('lo float', _bisect.bisect_left, a, 5, 1.5)
show('big lo', _bisect.bisect_left, a, 5, 2**70)
show('too few', _bisect.bisect_left, a)
show('too many', _bisect.bisect_left, a, 1, 2, 3, 4)
show('bad kw', _bisect.bisect_left, a, 1, nope=1)
show('dup kw', _bisect.bisect_left, a, 1, lo=0, x=1)
show('missing', _bisect.bisect_left, a=a)
show('not seq', _bisect.bisect_left, 5, 1)
show('mapping', _bisect.bisect_left, {1: 2}, 1)
show('mixed', _bisect.bisect_left, [1, 'a'], 1)
show('str', _bisect.bisect_left, 'abcdef', 'c')
show('tuple', _bisect.bisect_left, (1, 2, 3), 2)
show('floats', _bisect.bisect_left, [0.5, 1.5, 2.5], 1)
show('cmp raises', _bisect.bisect_left, [1, 2], object())
b = [1, 3, 5]
show('insort', _bisect.insort_left, b, 4)
show('insort_right', _bisect.insort_right, b, 4, key=lambda x: -x)
print(b)


class L:
    def __init__(self): self.l = [3, 4]
    def __len__(self): return len(self.l)
    def __getitem__(self, i): return self.l[i]
    def insert(self, i, x): print('insert', i, x); self.l.insert(i, x)


show('insort user', _bisect.insort_right, L(), 3)
show('insort type', _bisect.insort_left, 5, 3)


class Rec:
    pass


show('recursion', _bisect.bisect_left, [Rec(), Rec()], Rec())

h = []
for x in (5, 1, 4, 2, 3, 9, 0):
    _heapq.heappush(h, x)
print(h)
show('pop', _heapq.heappop, h)
show('pushpop', _heapq.heappushpop, h, 0)
show('pushpop2', _heapq.heappushpop, h, 7)
show('replace', _heapq.heapreplace, h, -1)
print(h)
g = [9, 8, 7, 6, 5, 4, 3, 2, 1]
show('heapify', _heapq.heapify, g)
print(g)
show('pop empty', _heapq.heappop, [])
show('replace empty', _heapq.heapreplace, [], 1)
show('pushpop empty', _heapq.heappushpop, [], 1)
show('push tuple', _heapq.heappush, (), 1)
show('push argc', _heapq.heappush, [])
show('push kw', _heapq.heappush, [], item=1)
show('heapify type', _heapq.heapify, 5)
show('nlargest-ish', _heapq.heappop_max, [1, 5, 3])
m = [1, 5, 3, 2]
show('heapify_max', _heapq.heapify_max, m)
print(m)
show('heapreplace_max', _heapq.heapreplace_max, m, 0)
print(m)
show('mixed', _heapq.heappush, [1, 'a'], 'b')
show('mixed2', _heapq.heappush, ['a', 1, 2], 0)
hs = [(2, 'b'), (1, 'a')]
_heapq.heapify(hs)
print(hs)


class Shrink:
    def __lt__(self, other):
        del box[:]
        return True


box = [Shrink(), Shrink(), Shrink()]
show('mutate', _heapq.heappush, box, Shrink())

show('inv_cdf', _statistics._normal_dist_inv_cdf, 0.5, 0.0, 1.0)
show('inv_cdf2', _statistics._normal_dist_inv_cdf, 0.975, 10.0, 2.5)
show('inv_cdf3', _statistics._normal_dist_inv_cdf, 1e-10, 0.0, 1.0)
show('inv_cdf4', _statistics._normal_dist_inv_cdf, 0.0, 0.0, 1.0)
show('inv_cdf5', _statistics._normal_dist_inv_cdf, 1.0, 0.0, 1.0)
show('inv_cdf int', _statistics._normal_dist_inv_cdf, 1, 2, 3)
show('inv_cdf str', _statistics._normal_dist_inv_cdf, 'a', 0.0, 1.0)
show('inv_cdf kw', _statistics._normal_dist_inv_cdf, p=0.3, mu=1.0, sigma=2.0)
show('inv_cdf argc', _statistics._normal_dist_inv_cdf, 0.3)
show('inv_cdf pos', _statistics._normal_dist_inv_cdf, 0.3, 1.0, 1.0, 1.0)
show('inv_cdf big', _statistics._normal_dist_inv_cdf, 0.3, 2**2000, 1.0)
