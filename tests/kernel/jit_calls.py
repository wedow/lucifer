# Calls the optimizing tier should inline once it has run: a method reached
# through a function that was compiled before the interpreter ever ran it
# (-X lucifer_jit=always compiles at the first call), and builtins whose
# kernel code calls primitives that run no Python (len of a dict or a str),
# and operations the interpreter answers in C without the kernel (subscripts,
# comparisons, attributes of several classes).
# Each result must be the interpreter's; tools/jitcheck also runs this at two
# sizes and requires that entries into compiled code do not grow with the
# iterations.
import sys

N = int(sys.argv[1]) if len(sys.argv) > 1 else 3000


class Node:
    def __init__(self, v):
        self.v = v

    def get(self):
        return self.v


def visit(n):
    return n.get() + 1


def methods(n):
    node = Node(3)
    s = 0
    for i in range(n):
        s += visit(node)
    return s


def lengths(n):
    d = {'a': 1, 'b': 2}
    t = 'xyz'
    s = 0
    for i in range(n):
        s += len(d) + len(t)
    return s


def generic(n, pair, other):
    """Subscripts and comparisons compiled as generic operations, which the
    interpreter answers in C (a tuple's item, tuples' equality)."""
    s = 0
    for i in range(n):
        s += pair[0]
        if pair == other:
            s += 1
    return s


class A:
    def __init__(self):
        self.x = 1


class B(A):
    pass


class C(A):
    pass


class D(A):
    pass


def attributes(n):
    """Attribute sites that met several classes and stay generic."""
    objs = [A(), B(), C(), D()]
    s = 0
    for i in range(n):
        o = objs[i & 3]
        o.y = i
        s += o.x + o.y
    return s


print(methods(N), lengths(N), generic(N, (1, 2), (1, 2)), attributes(N))
