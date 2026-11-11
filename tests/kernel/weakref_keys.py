# Weak references as dict and set keys: a reference hashes as its referent did
# and compares by the referents' own __eq__, never by identity alone, and by
# identity once either referent is gone (weakref_richcompare).
import gc
import weakref


class Never:
    """Hashes by identity, yet equal to nothing, itself included."""
    __hash__ = object.__hash__

    def __eq__(self, other):
        return False


class Value:
    def __init__(self, v):
        self.v = v

    def __hash__(self):
        return hash(self.v)

    def __eq__(self, other):
        return isinstance(other, Value) and other.v == self.v


def cb(ref):
    pass


a = Never()
r1, r2 = weakref.ref(a, cb), weakref.ref(a, cb)
print('distinct refs', r1 is not r2)
print('never equal', r1 == r2, r1 != r2, r2 in {r1: 1}, r2 in {r1})
print('hash', hash(r1) == hash(a) == hash(r2))

class Plain:
    pass


b = Plain()
s1, s2 = weakref.ref(b, cb), weakref.ref(b, cb)
print('identity referent', s1 == s2, s2 in {s1}, {s1: 'x'}[s2])

x, y = Value(3), Value(3)
t1, t2 = weakref.ref(x), weakref.ref(y)
print('value referents', t1 == t2, t2 in {t1}, hash(t1) == hash(t2))
words = [Value(w) for w in ('p', 'q')]
d = {weakref.ref(w): w.v for w in words}
probes = [Value(w) for w in ('p', 'q')]
print('lookups', [d[weakref.ref(p)] for p in probes])

h = hash(t1)
del x
gc.collect()
print('dead keeps hash', hash(t1) == h, t1() is None)
print('dead compares by identity', t1 == t2, t1 == t1, t1 in {t2}, t1 in {t1})
fresh = weakref.ref(Value(9))
gc.collect()
try:
    hash(fresh)
except TypeError as e:
    print('TypeError', e)
