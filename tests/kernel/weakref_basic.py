import _weakref

ref = _weakref.ref


class Box:
    pass


calls = []
box = Box()
r = ref(box)
r2 = ref(box)
print(r is r2, r() is box, _weakref.getweakrefcount(box), type(r).__name__, type(r).__module__)
r3 = ref(box, lambda dead: calls.append(("first", dead is r3)))
r4 = ref(box, lambda dead: calls.append(("second", dead is r4)))
print(r3 is r, _weakref.getweakrefcount(box), len(_weakref.getweakrefs(box)), r3.__callback__ is not None, r.__callback__)
print(r == r2, r == r3, hash(r) == hash(box), {r: 1}[r2])
print(bool(r), callable(r))
del box
print(r() is None, r2() is None, calls)
print(r == r2, r == r3, r == 3)
try:
    hash(ref(Box()))
except TypeError as e:
    print(e)
print(_weakref.getweakrefcount(Box()), _weakref.getweakrefs(Box()))

for thing in (1, "text", (1, 2), [1], {}):
    try:
        ref(thing)
    except TypeError as e:
        print(e)


class Keyed(ref):
    __slots__ = ("key",)

    def __init__(self, ob, callback, key):
        super().__init__(ob, callback)
        self.key = key


target = Box()
k = Keyed(target, None, "k")
print(k.key, k() is target, isinstance(k, ref), k is ref(target))
del target
print(k(), k.key)

p = _weakref.proxy(Box())
try:
    p.x
except ReferenceError as e:
    print(type(e).__name__, e)
holder = Box()
holder.value = [1, 2, 3]
px = _weakref.proxy(holder)
print(px.value, len(px.value), px.value[1], type(px).__name__, bool(px))
px.other = 5
print(holder.other)
del px.other
print(hasattr(holder, "other"))
d = {}
wr = ref(holder, lambda gone: d.pop("k", None))
d["k"] = wr
_weakref._remove_dead_weakref(d, "k")
print(len(d))
del holder
_weakref._remove_dead_weakref(d, "k")
print(len(d), wr())
