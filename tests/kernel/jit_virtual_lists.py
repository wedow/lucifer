# Lists, keyword dicts and bound methods the compiled code need not make: built, changed on
# some paths, joined, passed through f(*args, **kwargs), and read back as the objects they are.


def branches(a, b, flag):
    out = [a, b]
    if flag is None:
        out.append(99)
    else:
        out.append(flag)
    return [tuple(out)] + [(len(out),)]


def star(f, *args, **kwargs):
    return f(*args, **kwargs)


def target(a, b=2, *rest, c=3, **more):
    return a, b, rest, c, sorted(more.items())


class Box:
    def __init__(self, v):
        self.v = v

    def get(self, k=1):
        return self.v * k


def methods(box, n):
    total = 0
    for i in range(n):
        m = box.get
        total += m() + star(m, 2) + star(box.get, k=3)
    return total


def identity(n):
    seen = []
    for i in range(n):
        l = [i]
        t = (l, l)
        seen.append(t[0] is t[1] and l is not [i])
        if i % 7 == 0:
            l.append(l)
            seen.append(l[1] is l)
    return all(seen), len(seen)


def conversions(n):
    out = []
    for i in range(n):
        out.append((str(i), len(str(i * 1000)), tuple([i, i + 1]), list((i,)), dict(a=i)))
    return out[-2:]


for _ in range(300):
    r = (branches(1, 2, None), branches(1, 2, 5), star(target, 1), star(target, 1, 2, 3, c=4, d=5),
         star(target, *(7, 8), **{'z': 1}), methods(Box(3), 5), identity(20), conversions(12))
print(r)
try:
    star(target)
except TypeError as e:
    print(type(e).__name__, e)
try:
    star(target, 1, a=2)
except TypeError as e:
    print(type(e).__name__, e)
