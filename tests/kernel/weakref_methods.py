# Bound methods and builtin functions are weakly referenceable.
import gc
import weakref


class C:
    def method(self):
        return 'called'


c = C()
bound = c.method
r = weakref.ref(bound)
print(r() is bound, r()())
del bound
gc.collect()
print(r())
dead = []
r2 = weakref.ref(c.method, lambda ref: dead.append(ref))
gc.collect()
print(r2(), len(dead))
print(weakref.ref(len)() is len, weakref.ref([].append)())
wm = weakref.WeakMethod(c.method)
print(wm()())
finalized = weakref.finalize(c.method.__func__, print, 'never')
print(finalized.alive)
