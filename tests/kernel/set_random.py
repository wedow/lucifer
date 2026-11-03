# Random operation sequences on sets and dicts: the table's iteration order and size must follow
# the oracle's probing and resizing step by step. The generator is a fixed LCG.
state = 12345
def rnd(n):
    global state
    state = (state * 1103515245 + 12345) % 2147483648
    return (state >> 8) % n

def key(span):
    r = rnd(4)
    v = rnd(span)
    if r == 0:
        return v
    if r == 1:
        return -v
    if r == 2:
        return v * 1024
    return (v, v & 3)

for span in (8, 40, 300):
    s = set()
    d = {}
    for step in range(600):
        op = rnd(10)
        k = key(span)
        if op < 4:
            s.add(k)
            d[k] = step
        elif op < 7:
            s.discard(k)
            d.pop(k, None)
        elif op == 7:
            if s:
                s.pop()
            if d:
                d.popitem()
        elif op == 8:
            other = set([key(span) for i in range(rnd(12))])
            kind = rnd(5)
            if kind == 0:
                s |= other
                d.update(dict.fromkeys(other, step))
            elif kind == 1:
                s &= other
                d = {a: b for a, b in d.items() if a in other}
            elif kind == 2:
                s -= other
                for a in other:
                    d.pop(a, None)
            elif kind == 3:
                s ^= other
                for a in other:
                    if a in d:
                        del d[a]
                    else:
                        d[a] = step
            else:
                s = s.union(other)
        elif op == 9 and rnd(8) == 0:
            s.clear()
            d.clear()
        if step % 50 == 0:
            print(span, step, list(s), s.__sizeof__(), list(d), d.__sizeof__(), len(s), len(d))
    print(span, 'end', list(s), s.__sizeof__(), list(frozenset(s)), hash(frozenset(s)), list(d)[:20], d.__sizeof__())
a = set(range(0, 100, 3))
b = set(range(0, 100, 2))
print(list(a | b), list(a & b), list(a - b), list(a ^ b), list(b - a), list(frozenset(a) | b), list(a.union(range(50, 70))))
print(list(a.symmetric_difference(range(10, 40))), list(a.difference(range(10, 40))), list(a.intersection(range(10, 40))))
print(list(b.copy()), list(frozenset(b)), list(set(list(b))), list(set(sorted(b, reverse=True))))
