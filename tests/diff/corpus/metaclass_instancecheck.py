"""metaclass instancecheck."""
from support import loops


def run():
    class M(type):
        def __instancecheck__(cls, obj): return hasattr(obj, 'token')
        def __subclasscheck__(cls, sub): return sub.__name__ == 'Accepted'
    class C(metaclass=M): pass
    class Accepted: token = 1
    return isinstance(Accepted(), C), isinstance(1, C), issubclass(Accepted, C)


for _ in range(loops()):
    RESULT = run()
print(repr(RESULT))
