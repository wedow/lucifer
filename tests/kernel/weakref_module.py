import types
import weakref

m = types.ModuleType('weakly')
r = weakref.ref(m)
print(r() is m, type(r).__name__)
d = weakref.WeakValueDictionary({0: m})
print(d[0] is m, len(d))
import random
reg = weakref.WeakValueDictionary({0: random})
print(reg[0] is random)
print(weakref.getweakrefcount(m), weakref.getweakrefs(m) == [r])
