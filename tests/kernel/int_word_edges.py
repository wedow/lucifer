# Int operators on values at the edges of a digit and of a machine word,
# both signs, through the eval loop's operators (and, in a loop, compiled
# code): results that fit a word, results that just overflow one, floor
# division and modulo with every sign, division by zero, bools as ints.
edges = []
for e in (0, 1, 2, 15, 29, 30, 31, 32, 59, 60, 61, 62, 63, 64):
    for d in (-1, 0, 1):
        v = (1 << e) + d
        edges.append(v)
        edges.append(-v)
edges = sorted(set(edges)) + [True, False]

def ops(a, b):
    r = [a + b, a - b, a * b, a & b, a | b, a ^ b, a < b, a <= b, a == b, a != b, a > b, a >= b]
    if b:
        r += [a // b, a % b, divmod(a, b)]
    else:
        for f in (lambda: a // b, lambda: a % b):
            try:
                f()
            except ZeroDivisionError as e:
                r.append(str(e))
    x = a
    x += b
    x -= 3
    x *= 2
    r.append(x)
    if 0 <= b < 70:
        r += [a << b, a >> b]
    return r

check = 0
for a in edges:
    for b in edges:
        for c in repr(ops(a, b)).encode():
            check = (check * 31 + c) % (1 << 61)
print(check)

# The same pairs where the types are fixed in the loop: once compiled, the
# operands change sign and size mid-loop.
def loop(xs, ys):
    t = 0
    for x in xs:
        for y in ys:
            t += x + y
            t -= x * 3
            if y:
                t += x % y + x // y
            t ^= x & y
            if x < y:
                t += 1
    return t
small = [v for v in edges if not isinstance(v, bool) and -1000 < v < 1000]
for _ in range(3):
    print(loop(small, small))
print(loop(edges, edges))
print(type(True + True), True + True, True * 5, True % 2, False // 3, True & 1, type(True & True))

# Cached results are the cached ints, computed where the compiler cannot fold them.
def fd(a, b):
    return a // b
def md(a, b):
    return a % b
small = {v: v for v in (-5, 0, 2, 3)}
print(fd(7, 2) is small[3], md(-5, 7) is small[2], md(-5, 7), fd(-5, 7), fd(5, -1) is small[-5], md(0, -3) is small[0])
print(fd(-(2**30 - 1), -1), fd(-(2**30 - 1), -1) == 2**30 - 1)
# The ends of a machine word reached as results.
LLONG_MAX, LLONG_MIN = 2**63 - 1, -2**63
print(fd(LLONG_MAX, 1), fd(LLONG_MIN, 1), (2**62 - 1) * 2 + 1 == LLONG_MAX, -(2**62) * 2 == LLONG_MIN)
print(fd(LLONG_MIN, -1), md(LLONG_MIN, -1), (LLONG_MAX + 1) // 2, LLONG_MIN - 1, LLONG_MAX + 1)
# Result types of // and % for bools and int subclasses.
class I(int):
    pass
for a, b in ((True, True), (True, 2), (7, True), (I(7), 2), (7, I(2)), (I(7), I(2))):
    print(type(fd(a, b)).__name__, type(md(a, b)).__name__, fd(a, b), md(a, b))
# An int subclass's own operators run.
class J(int):
    def __floordiv__(self, other):
        return "floordiv"
    def __mod__(self, other):
        return "mod"
    def __lt__(self, other):
        return "lt"
def lt(a, b):
    return a < b
j = J(9)
print(fd(j, 2), md(j, 2), lt(j, 2), fd(9, J(2)), lt(1, J(2)))
