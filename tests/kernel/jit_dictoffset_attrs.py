# Hot attribute loads and stores on instances whose dict is at the type's
# tp_dictoffset (ast nodes, and a Python subclass of one, which inherits the
# offset): object's protocol, read and written through the dict by the
# dict hint forms. tools/jitcheck runs this at two sizes and requires that
# compiled code takes no generic attribute path per iteration. After the
# loop the instances' dicts are replaced, emptied, deleted from, shared and
# given a str-subclass key, and the class gains hooks: the same code must
# see each. One class adds __slots__ to the dict at the offset.
import ast
import sys

N = int(sys.argv[1]) if len(sys.argv) > 1 else 3000


class Node(ast.AST):
    pass


class Slotted(ast.AST):
    __slots__ = ('s',)


class S(str):
    pass


def hot(n, x, y):
    s = 0
    for j in range(n):
        x.b = j
        s += x.a + x.b + y.a + y.id + z.s + z.a
    return s


x = Node()
x.a = 1
y = ast.Name('v')
y.a = 2
y.id = 3
z = Slotted()
z.s = 4
z.a = 5
print(hot(N, x, y))
x.__dict__ = {'a': 10}
print(hot(3, x, y), sorted(vars(x)))
z.__dict__ = {}
try:
    hot(3, x, y)
except AttributeError as e:
    print('AttributeError', e)
z.a = 6
print(hot(3, x, y), vars(z))
del y.a
try:
    hot(3, x, y)
except AttributeError as e:
    print('AttributeError', e)
y.a = 4
x.__dict__ = y.__dict__
print(hot(3, x, y), y.b)
x.__dict__ = {S('a'): 20}
print(hot(3, x, y), sorted(map(type, vars(x)), key=str))
Node.__getattr__ = lambda self, attr: 0
Node.__getattribute__ = lambda self, attr: 100 if attr == 'a' else object.__getattribute__(self, attr)
print(hot(3, x, y))
del Node.__getattribute__, Node.__getattr__
seen = []
Node.__setattr__ = lambda self, attr, v: seen.append(attr)
print(hot(3, x, y), seen)
