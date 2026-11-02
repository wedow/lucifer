"""descriptor set name."""
from support import loops


def run():
    log = []
    class D:
        def __set_name__(self, owner, name): log.append((owner.__name__, name))
        def __get__(self, obj, cls): return cls.__name__
    class A: x = D()
    class B(A): pass
    return log, A().x, B().x


for _ in range(loops()):
    RESULT = run()
print(repr(RESULT))
