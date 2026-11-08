# Calls of classes whose __new__ is a kernel function of their own and
# whose __init__ is object's (str, int, float, bytes, tuple, range,
# frozenset, and a subclass of str), with and without keywords:
# type.__call__ returns what __new__ returns, so compiled code calls
# __new__ itself, with the class first. tools/jitcheck runs this at two
# sizes and requires that no call per iteration reaches the generic call
# of a class (-X lucifer_jit_stats' whynot type). After the loop, errors
# __new__ raises and a class whose __init__ changes under the compiled
# code must be the interpreter's.
import sys

N = int(sys.argv[1]) if len(sys.argv) > 1 else 3000


class Tag(str):
    pass


class Pair(tuple):
    pass


def hot(n):
    s = 0
    for i in range(n):
        s += len(str(i)) + int('12') + int('ff', base=16) + int(float(i))
        s += len(bytes(3)) + len(Tag('ab')) + len(tuple((1, 2))) + len(Pair([i, i]))
        s += len(range(i, i + 3)) + len(frozenset((1, 2, 2))) + len(str(b'x', encoding='ascii'))
    return s


print(hot(N))
print(type(Tag('x')).__name__, type(Pair((1,))).__name__, str(object=5))
for call in (lambda: int('z'), lambda: int('1', base=1), lambda: str(1, 2), lambda: bytes(-1), lambda: range(),
             lambda: str(b'\xff', encoding='ascii'), lambda: int(y=1)):
    try:
        print(call())
    except (TypeError, ValueError, UnicodeDecodeError) as e:
        print(type(e).__name__, e)
Tag.__init__ = lambda self, *a: print('init', a)
print(hot(3))
