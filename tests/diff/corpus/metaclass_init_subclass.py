"""metaclass init subclass."""
from support import loops


def run():
    log = []
    class A:
        def __init_subclass__(cls, flag=0, **kw):
            super().__init_subclass__(**kw); log.append((cls.__name__, flag))
    class B(A, flag=7): pass
    class C(B): pass
    return log


for _ in range(loops()):
    RESULT = run()
print(repr(RESULT))
