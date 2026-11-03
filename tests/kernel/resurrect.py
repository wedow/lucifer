# Finalizers, weakref callbacks and resurrection under the cycle collector
# (PEP 442): a __del__ runs once, even when it resurrects its object, and a
# weakref callback runs before it for garbage in a cycle, after it for an
# object whose count drops to zero.
import gc
import weakref

gc.disable()
log = []


def collect():
    gc.collect()
    gc.collect()


# A __del__ that resurrects its object runs once.
survivors = []


class Phoenix:
    def __del__(self):
        log.append("phoenix del")
        survivors.append(self)


p = Phoenix()
p.me = p
r = weakref.ref(p, lambda ref: log.append("phoenix callback"))
del p
collect()
print("resurrected:", log, len(survivors), r() is survivors[0])
print("finalized:", gc.is_finalized(survivors[0]), gc.is_tracked(survivors[0]))
survivors.clear()
collect()
print("second death:", log, r() is None)

# A weakref callback comes before __del__ for garbage in a cycle...
log.clear()


class Both:
    def __del__(self):
        log.append("del")


b = Both()
b.me = b
rb = weakref.ref(b, lambda ref: log.append(("callback", ref() is None)))
del b
collect()
print("cycle order:", log)

# ...and after it for an object that dies at once.
log.clear()
c = Both()
rc = weakref.ref(c, lambda ref: log.append(("callback", ref() is None)))
del c
print("refcount order:", log)


# A weakref that is itself garbage does not call back.
log.clear()


class Holder:
    pass


h = Holder()
h.me = h
h.ref = weakref.ref(h, lambda ref: log.append("holder callback"))
del h
collect()
print("garbage weakref:", log)

# A WeakValueDictionary loses the values of collected cycles.
wvd = weakref.WeakValueDictionary()
keep = Holder()
for i in range(5):
    v = Holder()
    v.me = v
    wvd[i] = v
wvd["keep"] = keep
del v
collect()
print("wvd:", sorted(wvd, key=str), list(wvd.values()) == [keep])

# A chain of cyclic garbage with finalizers is finalized once, in one pass.
log.clear()


class Node:
    def __init__(self, name, other=None):
        self.name = name
        self.other = other

    def __del__(self):
        log.append(self.name)


a = Node("a")
z = Node("z", a)
a.other = z
del a, z
collect()
print("pair:", sorted(log))

# gc.garbage with DEBUG_SAVEALL keeps what it collects.
gc.set_debug(gc.DEBUG_SAVEALL)
s = []
s.append(s)
sid = id(s)
del s
collect()
gc.set_debug(0)
print("saved:", any(id(o) == sid for o in gc.garbage))
del gc.garbage[:]

# get_referents, get_referrers, freeze.
inner = []
outer = [inner]
print("referents:", gc.get_referents(outer) == [inner])
print("referrers:", any(o is outer for o in gc.get_referrers(inner)))
gc.freeze()
print("frozen:", gc.get_freeze_count() > 0)
gc.unfreeze()
print("unfrozen:", gc.get_freeze_count())
