# luc-1kz0: a large callee, inlined into a hot loop, whose early places
# meet their first objects only in the run's second half (an arm and a loop
# body the first half never runs). A rebuild for what those places learned
# is predicted at the unit's own build, never less: learning there folds the
# places' own generic paths, not the rest of the callee after them.
# tools/jitcheck runs this with the tier's event log and requires each such
# rebuild to be predicted at least at the build of the unit it replaces.
import sys


class Rec:
    def __init__(self, i):
        self.a = i
        self.b = i + 1
        self.c = i * 2
        self.d = i % 7
        self.tags = []


def handle(r, late):
    x = 0
    if late:
        x = r.a * 3 + r.b
    for t in r.tags:
        x += t.a
    y = r.a + r.b
    z = r.c - r.d
    w = y * z + x
    if w % 3 == 0:
        w += r.a
    else:
        w -= r.b
    if r.d > 3:
        w = w * 2 + r.c
    else:
        w = w + r.d * 5
    v = (w ^ r.a) & 0xffff
    u = v + r.b * r.c - r.d
    if u > 1000:
        u = u % 997 + r.a
    s = u + v + w + y + z
    if s & 1:
        s += r.c
    else:
        s -= r.d
    q = s * 3 + u - v
    return q % 100003


def main(n):
    recs = [Rec(i) for i in range(64)]
    total = 0
    for i in range(n):
        r = recs[i & 63]
        if i == n // 2:
            for k in range(0, 64, 2):
                recs[k].tags = [Rec(k + 1)]
        total += handle(r, i >= n // 2)
    print(total)


main(int(sys.argv[1]) if len(sys.argv) > 1 else 20000)
