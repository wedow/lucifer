# Calls of classes whose __new__ is their builtin base's kernel function
# and whose __init__ is the base's own or a Python function (list, set,
# bytearray, an exception class, a list subclass with and without its own
# __init__): type.__call__'s work in line, __new__ called and __init__
# inlined. tools/jitcheck runs this at two sizes (lane `kclasses`) and
# requires that no call per iteration reaches the generic call of a class
# (-X lucifer_jit_stats' whynot type). After the loop, errors raised by
# __init__ and a class whose __init__ changes under the compiled code must
# be the interpreter's.
import sys

N = int(sys.argv[1]) if len(sys.argv) > 1 else 3000


class Bag(list):
    pass


class Named(list):
    def __init__(self, name, items):
        super().__init__(items)
        self.name = name


class Oops(ValueError):
    pass


def hot(n):
    s = 0
    src = (1, 2, 3)
    for i in range(n):
        s += len(list(src)) + len(set(src)) + len(bytearray(i & 7)) + len(Bag(src))
        s += len(Named('n', src)) + len(StopIteration(i).args) + len(Oops('x', i).args)
    return s


print(hot(N))
print(Named('a', [1]).name, Bag([2]), Oops(1, 2), repr(StopIteration(3).value))
for call in (lambda: list(5), lambda: set(1, 2), lambda: bytearray('x'), lambda: Named('a'), lambda: Bag(None)):
    try:
        print(call())
    except TypeError as e:
        print(type(e).__name__, e)
Bag.__init__ = lambda self, *a: print('init', a)
print(hot(3))
