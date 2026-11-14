# Objects compiled code never makes, when something needs them after all:
# a tuple, an int and a float held in locals keep one identity through an
# exit, through a walker's look and through the exit after it; an object
# made on a cold path is the one later code sees; what a loop carries
# unboxed comes out as the interpreter's values, whatever turns up.
import sys

N = int(sys.argv[1]) if len(sys.argv) > 1 else 3000
seen = []


def look():
    f = sys._getframe(1)
    seen.append(f.f_locals['pair'])
    seen.append(f.f_locals['big'])


def identity(n):
    out = []
    for i in range(n):
        pair = (i, i + 1)
        big = i * 1.5 + 1e9
        alias = pair
        same = big
        if i == n - 3:
            look()
            out.append(seen[0] is pair)
            out.append(seen[0] is alias)
            out.append(seen[1] is big)
            out.append(seen[1] is same)
        if i == n - 2:
            out.append(pair is alias)
            out.append(big is same)
            out.append([pair, alias][0] is [pair, alias][1])
        if i == n - 1:
            out.append((pair, alias))
    return out


def cold_escape(n):
    kept = []
    total = 0
    for i in range(n):
        t = (i, str(i))
        total += t[0]
        if i % 1000 == 999:
            kept.append(t)
            kept.append(t)
    return total, kept, [a is b for a, b in zip(kept[::2], kept[1::2])]


def carried(n, start, step):
    acc = start
    for i in range(n):
        if i == n // 2:
            acc = acc + step
        acc = acc + i
    return acc


def swap(n):
    a, b = 1, 2.5
    for i in range(n):
        a, b = b, a
        if i % 2:
            a = a + 1
    return a, b


def unpack(n):
    s = 0
    for i in range(n):
        x, y = divmod(i, 7) if i % 500 == 0 else (i, 1)
        q, (r, w) = i, (x, y)
        s += q + r * w
    return s


def small_ints(n):
    out = 0
    for i in range(n):
        a = i % 100
        b = (i + 100) % 100
        if a is b:
            out += 1
    return out


class Box:
    def __init__(self, v):
        self.v = v


def nested(n):
    last = None
    for i in range(n):
        t = ((i, i + 0.5), [i])
        if i % 1500 == 1499:
            last = Box(t)
    return last.v, last.v[0] is last.v[0], type(last.v[0][1])


print(identity(N))
print(cold_escape(N))
print(carried(N, 0, 1), carried(N, 0, 0.5), carried(N, 1.5, 1), carried(N, 2 ** 70, 1))
print(swap(N))
print(unpack(N))
print(small_ints(N))
print(nested(N))
