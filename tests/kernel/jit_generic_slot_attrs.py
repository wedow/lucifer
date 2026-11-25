# Hot attribute loads and stores on instances of Python subclasses of a C
# type whose attribute slots are object's generic functions (decimal.Decimal:
# its own __getattribute__, __setattr__ and __delattr__ are C slot wrappers
# of them, and it has no dict, so its subclasses get a managed one). They
# use object's protocol and take the specialized forms. tools/jitcheck runs
# this at two sizes and requires that compiled code calls no helper per
# iteration: each load and store is a guarded access at the shape the site
# learned. After the loop the class gains hooks and the same code must see
# them.
import decimal
import sys

N = int(sys.argv[1]) if len(sys.argv) > 1 else 3000


class D(decimal.Decimal):
    pass


class E(D):
    pass


def hot(n, x, y):
    s = 0
    for j in range(n):
        x.b = j
        y.b = j
        s += x.b + y.b
    return s


x = D(1)
y = E(2)
print(hot(N, x, y))
D.__getattr__ = lambda self, attr: 0
D.__getattribute__ = lambda self, attr: 10 if attr == 'b' else object.__getattribute__(self, attr)
print(hot(3, x, y))
