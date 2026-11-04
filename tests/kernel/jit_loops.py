# Hot loops of the shapes the optimizing tier compiles: unboxed ints and
# floats, list items, instance attributes, inlined methods and builtins,
# and every way compiled code has to leave: a guard that fails after
# warm-up, an int that outgrows a machine word, an exception from inside
# the loop, a rebound global, a replaced method. Each result must be the
# interpreter's.
import sys

N = 3000


def int_sum(n):
    s = 0
    for i in range(n):
        s += i * 3 - 1
    return s


def int_overflow(n):
    x = 1
    for _ in range(n):
        x = x * 3 + 1
    return x


def int_ops(n):
    a, b, acc = 7, 3, 0
    i = 0
    while i < n:
        acc ^= (a * i + b) & 0xFFFF
        acc += (i // 3) - (i % 7) + (-i >> 2) + (i << 1)
        i += 1
    return acc


def float_mix(n):
    x, y = 0.5, 1.25
    for i in range(n):
        x = x * 0.999 + y / (i + 1.0)
        y = y - x * 0.001
        if x > 2.0:
            x -= 1.0
    return (x, y)


def sieve(size):
    flags = [True] * size
    count = 0
    for i in range(2, size + 1):
        if flags[i - 1]:
            count += 1
            k = i + i
            while k <= size:
                flags[k - 1] = False
                k += i
    return count


class Point:
    def __init__(self, x, y):
        self.x = x
        self.y = y

    def step(self, dx):
        self.x = self.x + dx
        self.y += 1
        return self.x > 100


def attrs(n):
    p = Point(0, 0)
    hits = 0
    for i in range(n):
        if p.step(i % 5):
            hits += 1
            p.x = 0
    return (p.x, p.y, hits)


def floats_attrs(n):
    p = Point(0.5, 1.5)
    for i in range(n):
        p.x = p.x * 0.5 + p.y
        p.y = p.y - 0.25 * p.x
    return (round(p.x, 9), round(p.y, 9))


def builtins_loop(n):
    vals = [3, -4, 5, -6]
    out = 0
    for i in range(n):
        out += abs(vals[i % len(vals)]) + len(vals)
    return out


def type_change(n):
    # Ints until the middle, then floats: the int guard fails after warm-up.
    acc = 0
    for i in range(n):
        v = i if i < n // 2 else i + 0.5
        acc = acc + v
    return acc


def list_change(n):
    items = [1] * 10
    total = 0
    for i in range(n):
        if i == n // 2:
            items[3] = "x"
        v = items[i % 10]
        total += 1 if v == "x" else v
    return total


def raises_inside(n):
    caught = 0
    for i in range(n):
        try:
            if i % 500 == 499:
                1 // (i - i)
            caught += 0
        except ZeroDivisionError:
            caught += 1
    return caught


def raises_out(n):
    total = 0
    for i in range(n):
        total += 10 // (n - 1 - i)
    return total


G = 5


def global_rebind(n):
    global G
    total = 0
    for i in range(n):
        if i == n // 2:
            G = 7
        total += G
    G = 5
    return total


class Shape:
    def area(self):
        return 1


def method_replace(n):
    s = Shape()
    total = 0
    for i in range(n):
        if i == n // 2:
            Shape.area = lambda self: 2
        total += s.area()
    del Shape.area
    Shape.area = lambda self: 1
    return total


def none_checks(n):
    link = None
    nodes = []
    for i in range(n):
        node = Point(i, link)
        link = node
        if i % 3 == 0:
            nodes.append(node)
    count = 0
    while link is not None:
        count += 1
        link = link.y if None != link.y else None
    return (count, len(nodes))


def fib(n):
    return n if n < 2 else fib(n - 1) + fib(n - 2)


def negative_range(n):
    s = 0
    for i in range(n, 0, -3):
        s += i
    return s


def early_exit(n):
    for i in range(n):
        if i * i > n:
            return i
    return -1


def same_object(n):
    # A boxed value keeps one identity, however often the loop copies it.
    x = 1.5 * n
    y = x
    for _ in range(n):
        y = x
    return y is x


def frame_view(n):
    # A caller's locals, read through its frame from a callee, are current.
    def peek():
        return sys._getframe(1).f_locals["i"]
    seen = 0
    for i in range(n):
        if i == n - 1:
            seen = peek()
    return seen


def big_compare(n):
    a = 2 ** 70
    c = 0
    for i in range(n):
        if a > i:
            c += 1
    return c


for f, arg in [(int_sum, N), (int_overflow, 200), (int_ops, N), (float_mix, N), (sieve, 5000),
               (attrs, N), (floats_attrs, N), (builtins_loop, N), (type_change, N),
               (list_change, N), (raises_inside, N), (global_rebind, N), (method_replace, N),
               (none_checks, 600), (fib, 20), (negative_range, N), (early_exit, N),
               (same_object, N), (frame_view, N), (big_compare, N)]:
    print(f.__name__, f(arg))
try:
    raises_out(N)
except ZeroDivisionError as e:
    print("raises_out", type(e).__name__, e)
