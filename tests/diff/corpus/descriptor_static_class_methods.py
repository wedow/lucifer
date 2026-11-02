"""descriptor static class methods."""
from support import loops


def run():
    class A:
        @staticmethod
        def s(x): return x + 1
        @classmethod
        def c(cls): return cls.__name__
    class B(A): pass
    return A.s(3), B().s(4), A.c(), B().c()


for _ in range(loops()):
    RESULT = run()
print(repr(RESULT))
