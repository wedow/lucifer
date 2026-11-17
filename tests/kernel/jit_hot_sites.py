# Calls in a loop, made out of line because the calls before them took the
# room for calls in line first, then taken in line by how often the loop
# makes them: the unit is built again while it runs its loop. The calls make
# objects, step a generator, raise and catch, and change under the compiled
# code after it was built again.

# Straight-line callees, each longer than the room left for the next.
for k in range(6):
    exec(f'def _first{k}(x):\n    s = x\n' + '    s = (s * 31 + 7) & 0xffff\n' * 60 + '    return s\n')


class P:
    __slots__ = ('x', 'y')

    def __init__(self, x, y):
        self.x = x
        self.y = y

    def __add__(self, other):
        return P(self.x + other.x, self.y + other.y)


def step(p, i):
    q = p + P(i & 7, i & 3)
    return q.x - q.y


def evens(n):
    for i in range(n):
        if i % 2 == 0:
            yield i


def careful(i):
    try:
        if i % 1000 == 999:
            raise ValueError(i)
        return 1
    except ValueError as e:
        return -e.args[0]


def scale(i):
    return i * 2


def run(n):
    s = 0
    p = P(1, 2)
    g = evens(10 * n)
    total = 0
    for i in range(n):
        if i & 1:
            s ^= _first0(i) + _first1(i) + _first2(i) + _first3(i) + _first4(i) + _first5(i)
        total += step(p, i)
        total += next(g)
        total += careful(i)
        total += scale(i)
        if i == n // 2:
            globals()['scale'] = lambda i: i * 3
    return s, total


for n in (10, 1000, 40000):
    print(n, run(n))
