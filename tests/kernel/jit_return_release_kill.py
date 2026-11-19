"""A compiled function's return releases a local whose finalizer rebinds a
global the function's unit relies on, killing the unit during the release.
The return already has its value: the caller gets it, whatever the unit's
fate (luc-349f, luc-cqlp)."""
G = 0
log = []


class Killer:
    def __del__(self):
        global G
        G += 1
        log.append(G)


def f(n):
    k = Killer()
    x = G
    return n + x


out = list(map(f, range(3000)))
print(sum(out), len(out), len(log), G)
