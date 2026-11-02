"""class mro diamond."""
from support import loops


def run():
    class A:
        def f(self): return ['A']
    class B(A):
        def f(self): return ['B'] + super().f()
    class C(A):
        def f(self): return ['C'] + super().f()
    class D(B, C):
        def f(self): return ['D'] + super().f()
    return D().f(), [c.__name__ for c in D.__mro__]


for _ in range(loops()):
    RESULT = run()
print(repr(RESULT))
