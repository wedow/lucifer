"""The eval loop's attribute, call and container shortcuts against the
protocols they skip: descriptors, __getattr__, instance __class__,
subclass overrides, and specialized sites whose class changes."""

# 1. data descriptor __get__ raising AttributeError falls back to __getattr__
class D:
    def __get__(self, o, t):
        raise AttributeError('from get')
    def __set__(self, o, v):
        pass
class C:
    d = D()
    def __getattr__(self, n):
        return 'fallback ' + n
c = C()
for _ in range(3):
    try:
        print(1, c.d)
    except AttributeError as ex:
        print(1, "AE", ex)

# 2. descriptor with __delete__ only blocks plain stores
class Del:
    def __get__(self, o, t):
        return 'g'
    def __delete__(self, o):
        print('deleted')
class E:
    x = Del()
e = E()
for _ in range(3):
    try:
        e.x = 1
        print(2, 'stored', e.__dict__)
    except AttributeError as ex:
        print(2, 'AttributeError', ex)
    try:
        setattr(e, 'x', 1)
        print(2, 'setattr stored', e.__dict__)
    except AttributeError as ex:
        print(2, 'setattr AttributeError', ex)

# 3. isinstance honours an instance-level __class__
class Fake:
    def __getattribute__(self, n):
        if n == '__class__':
            return int
        return object.__getattribute__(self, n)
f = Fake()
for _ in range(3):
    print(3, isinstance(f, int), isinstance(f, Fake), f.__class__)

# 4. len, bool, in, dict ops on subclasses
class L(list):
    def __len__(self): return 99
    def __contains__(self, x): return True
    def __bool__(self): return False
class Dd(dict):
    def __getitem__(self, k): return 'sub'
    def __contains__(self, k): return True
    def get(self, k, d=None): return 'subget'
for _ in range(3):
    l = L([1]); dd = Dd(a=1)
    print(4, len(l), 5 in l, bool(l), dd['a'], 'zz' in dd, dd.get('a'), dd.get('q'))

# 5. str subclass key with custom hash/eq
class S(str):
    def __hash__(self): return hash('a')
    def __eq__(self, o): return True
d = {S('zzz'): 1}
for _ in range(3):
    print(5, d.get('a'), 'a' in d, d['a'] if 'a' in d else None)
    d2 = {}
    d2[S('q')] = 1
    d2['a'] = 2
    print(5, list(d2.items()))

# 6. dict mutated during iteration
d = {'a': 1, 'b': 2}
try:
    for k in d:
        d['c' + k] = 1
except RuntimeError as ex:
    print(6, 'RuntimeError', ex)
d = {'a': 1, 'b': 2}
try:
    for k, v in d.items():
        del d['b']
except RuntimeError as ex:
    print(6, 'RuntimeError', ex)
d = {'a': 1, 'b': 2}
try:
    for v in d.values():
        d.pop('a'); d['a'] = 3
    print(6, 'no error', d)
except RuntimeError as ex:
    print(6, 'RuntimeError', ex)

# 7. setattr past non-data class attributes, and descriptors
class NonData:
    def __get__(self, o, t): return 'nd'
class F:
    m = NonData()
    k = 5
    def meth(self): return 'meth'
fo = F()
for _ in range(2):
    fo.m = 'inst'; setattr(fo, 'k', 6); fo.meth = 'shadow'
    print(7, fo.m, fo.k, fo.meth, F.k, F.m)

# 8. metaclass attributes
class Meta(type):
    attr = 'meta'
    @property
    def prop(cls): return 'metaprop'
class M(metaclass=Meta):
    attr = 'class'
    prop = 'classprop'
for _ in range(3):
    print(8, M.attr, M.prop, M.__class__, type(M).attr)
class Meta2(type):
    def __getattribute__(cls, n):
        return 'm2 ' + n
class M2(metaclass=Meta2):
    x = 1
for _ in range(3):
    print(8, M2.x, M2.__class__)

# 9. __class__ properties
class P:
    @property
    def __class__(self): return int
for _ in range(3):
    print(9, P().__class__, isinstance(P(), int))

# 10. set/tuple containment with odd elements
class Eq:
    def __eq__(self, o): return o == 7
    def __hash__(self): return 7
for _ in range(3):
    print(10, 7 in {Eq()}, 7 in (Eq(),), 7 in [Eq()], 'x' in frozenset('xyz'), 1.0 in {1}, 1 in {1.0}, True in {1})

# 11. bool on odd values
class B:
    def __len__(self): return 0
for _ in range(3):
    print(11, bool(B()), bool([]), bool(''), bool(0.0), bool(None))

# 12. getattr via native getset on builtin and member descriptors
class Sl:
    __slots__ = ('a', 'b')
s = Sl()
for _ in range(3):
    try:
        print(12, s.a)
    except AttributeError as ex:
        print(12, 'AttributeError', ex)
    s.a = 1
    print(12, s.a)
    del s.a
class SlG:
    __slots__ = ('a',)
    def __getattr__(self, n): return 'ga ' + n
for _ in range(3):
    print(12, SlG().a)

# 13. bytes indexing
b = b'\x00\xff'
for _ in range(2):
    print(13, b[0], b[1], b[-1])
    try:
        b[2]
    except IndexError as ex:
        print(13, 'IndexError', ex)


# Specialized sites after the class changes.
import gc
class A:
    __slots__ = ('a', 'b')
class B:
    pass
def loop_store(objs):
    for o in objs:
        o.a = len(str(o.__class__.__name__))
def loop_load(objs):
    out = []
    for o in objs:
        try:
            out.append(o.a)
        except AttributeError as e:
            out.append('AE')
    return out
xs = [A() for _ in range(40)]
loop_store(xs); print(21, loop_load(xs)[:3])
# replace the slot by a property
A.a = property(lambda s: 'prop', lambda s, v: print('setter', v))
loop_store(xs[:2]); print(22, loop_load(xs)[:3])
# dict-backed store then a data descriptor appears
bs = [B() for _ in range(40)]
loop_store(bs); print(23, loop_load(bs)[:3])
class Guard:
    def __get__(s, o, t): return 'guard'
    def __set__(s, o, v): print('guard set', v)
B.a = Guard()
loop_store(bs[:2]); print(24, loop_load(bs)[:3])
del B.a
# instance __dict__ replaced with a dict subclass
class DS(dict):
    def __setitem__(self, k, v):
        print('ds setitem', k); dict.__setitem__(self, k, v)
b = B(); b.__dict__ = DS()
loop_store([b]); print(25, loop_load([b]), type(b.__dict__).__name__)
# __class__ assignment between dict-backed classes
class C1: pass
class C2:
    @property
    def a(self): return 'C2prop'
cs = [C1() for _ in range(40)]
loop_store(cs)
cs[0].__class__ = C2
try:
    loop_store(cs[:1])
except AttributeError as e:
    print(26, 'AE', e)
print(26, loop_load(cs[:2]))
# store then delete via the slot path
xs2 = [type('S', (), {'__slots__': ('a',)})() for _ in range(3)]
def stdel(o):
    o.a = [1]
    del o.a
    try:
        o.a
    except AttributeError as e:
        return str(e)
for o in xs2 * 20:
    r = stdel(o)
print(27, r)
# refcounts: slot store releases the old value
import weakref
class W: pass
class SS:
    __slots__ = ('a',)
s = SS()
w = None
for i in range(30):
    v = W()
    if i == 10:
        w = weakref.ref(v)
    s.a = v
    del v
gc.collect()
print(28, w() is None)
class DD: pass
dd = DD()
for i in range(30):
    v = W()
    if i == 10:
        w = weakref.ref(v)
    dd.a = v
    del v
print(29, w() is None)
