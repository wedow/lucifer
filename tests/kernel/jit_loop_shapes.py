# Loops whose values change kind under compiled code: a local that is an
# int, then a float, then something else; an accumulator that overflows a
# machine word; a loop entered in the middle of an outer one; an iterator
# that leaves its loop; a sequence that changes while it is iterated. Each
# result must be the interpreter's.
import sys

N = int(sys.argv[1]) if len(sys.argv) > 1 else 3000


def kinds(n):
    x = 0
    out = []
    for i in range(n):
        if i == n // 3:
            x = float(x)
        elif i == 2 * n // 3:
            x = str(int(x))
        if isinstance(x, str):
            x = x[-3:] + str(i % 10)
        else:
            x = x + i
        if i % (n // 6) == 0:
            out.append(x)
    return out


def overflow(n):
    x = 1
    for i in range(n):
        x = x * 3 + i
        if i % 97 == 0:
            x = x % (2 ** 61)
    big = 2 ** 62
    for i in range(n):
        big += i * (2 ** 40)
    return x, big


def nested(n):
    total = 0
    count = 0
    for i in range(60):
        row = i * 1.0
        for j in range(n // 30):
            total += j * i
            row += 0.5
        count += int(row)
        if i % 7 == 0:
            total = total - count
    return total, count


def none_then_int(n):
    best = None
    for i in range(n):
        v = (i * 7919) % 1009
        if best is None or v > best:
            best = v
    return best


def escaping_iterator(n):
    data = list(range(n))
    kept = None
    s = 0
    for v in data:
        s += v
    it = iter(data)
    for v in it:
        s += v
        if v == n // 2:
            kept = it
            break
    return s, next(kept), sum(1 for _ in kept)


def growing(n):
    data = [1, 2, 3]
    s = 0
    for v in data:
        s += v
        if len(data) < n:
            data.append(v + 1)
    t = (1, 2, 3)
    for v in t:
        s += v
    return s, len(data)


def while_shapes(n):
    i = 0
    acc = 0
    flag = False
    while i < n:
        if i % 3 == 0:
            flag = not flag
        acc += i if flag else -i
        i += 1 if i < n - 10 else 2
    return i, acc, flag


def unbound_then_bound(n):
    for i in range(n):
        if i > 5:
            last = i
    return last


def tuples_carried(n):
    p = (0, 1)
    for i in range(n):
        p = (p[1], (p[0] + p[1]) % 1000003)
    return p


print(kinds(N))
print(overflow(N))
print(nested(N))
print(none_then_int(N))
print(escaping_iterator(N))
print(growing(N))
print(while_shapes(N))
print(unbound_then_bound(N))
print(tuples_carried(N))
