# Calls the optimizing tier should inline once it has run: a method reached
# through a function that was compiled before the interpreter ever ran it
# (-X lucifer_jit=always compiles at the first call), and builtins whose
# kernel code calls primitives that run no Python (len of a dict or a str),
# and operations the interpreter answers in C without the kernel (subscripts,
# comparisons, attributes of several classes).
# Each result must be the interpreter's; tools/jitcheck also runs this at two
# sizes and requires that entries into compiled code do not grow with the
# iterations, and that the tier gives up on no function.
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


def pops(n):
    """Kernel code with int constants past one digit (list.pop's index check
    against -sys.maxsize - 1): compiled arithmetic on them never leaves."""
    items = [0]
    s = 0
    for i in range(n):
        items.append(i)
        s += items.pop(0)
    return s


def nonzero(a, b, x):
    return a if x else b


def truths(n):
    """The truth of ints of no known type in an inlined frame (a third
    argument: calls record the kinds of their first two)."""
    items = [0, 1, -5, 1 << 40]
    s = 0
    for i in range(n):
        s += nonzero(1, 0, items[i & 3])
    return s


def clamp(x, hi):
    return x if x < hi else hi


def wide(n):
    """Ints past one digit that fit 64 bits, of no known value, through an
    inlined comparison and arithmetic."""
    bounds = [sys.maxsize, 1 << 40]
    s = 0
    for i in range(n):
        s += clamp(i, bounds[i & 1]) - (bounds[i & 1] >> 20)
    return s


print(methods(N), lengths(N), generic(N, (1, 2), (1, 2)), attributes(N), pops(N), truths(N), wide(N))
