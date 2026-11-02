"""closure class scope."""
from support import loops


def run():
    def outer():
        x = 'outer'
        class C:
            x = 'class'
            def f(self): return x, __class__.__name__
        return C().f(), C.x
    return outer()


for _ in range(loops()):
    RESULT = run()
print(repr(RESULT))
