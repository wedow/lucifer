# luc-1kz0: a function entered on every iteration that calls a large callee
# at many sites, more than its first build's budget takes in line. The
# callee loses on its own generic paths: its loop reads attributes of
# objects of more classes than an attribute site keeps, which a caller
# cannot tell apart either. Its calls out of line, valued with those losses,
# buy a doubling of the caller's budget; the grown unit takes more of the
# calls in line, but the callee's generic reads stay generic in the copies,
# so the losses per entry hardly fall. tools/jitcheck runs this with the
# tier's event log and requires that the caller's start grew once and not
# again: a doubling that removes less than half of what bought it is not
# followed by the next.
import sys


class A:
    def __init__(self, i): self.p = i; self.q = 1
class B:
    def __init__(self, i): self.q = 2; self.p = i
class C:
    def __init__(self, i): self.r = 0; self.p = i; self.q = 3
class D:
    def __init__(self, i): self.q = 4; self.r = 0; self.p = i
class E:
    __slots__ = ('p', 'q')
    def __init__(self, i): self.p = i; self.q = 5
class F:
    __slots__ = ('q', 'p')
    def __init__(self, i): self.q = 6; self.p = i
class G:
    def __init__(self, i): self.s = 0; self.r = 0; self.q = 7; self.p = i
class H:
    q = 8
    def __init__(self, i): self.p = i


def work(objs, k):
    s = 0
    for o in objs:
        s += o.p * k - o.q
    if s & 1:
        s += s * k
    else:
        s -= s // (k + 1)
    if s % 3 == k % 3:
        s ^= k << 2
    t = (s + k) * (s - k)
    if t > 1000:
        s += t % 97
    elif t < -1000:
        s -= t % 89
    else:
        s += t
    u = (s ^ k) % (k + 7)
    if u > 3:
        s += u * 3
    elif u == 2:
        s -= k
    else:
        s += s % 11
    return s


def entry(objs, x):
    return (work(objs, x) + work(objs, x + 1) + work(objs, x + 2) + work(objs, x + 3)
            + work(objs, x + 4) + work(objs, x + 5) + work(objs, x + 6) + work(objs, x + 7)
            + work(objs, x + 8) + work(objs, x + 9) + work(objs, x + 10) + work(objs, x + 11)
            + work(objs, x + 12) + work(objs, x + 13) + work(objs, x + 14) + work(objs, x + 15)
            + work(objs, x + 16) + work(objs, x + 17) + work(objs, x + 18) + work(objs, x + 19)
            + work(objs, x + 20) + work(objs, x + 21) + work(objs, x + 22) + work(objs, x + 23))


# The loop is module code, which stays in the interpreter: entry is its own unit's root.
n = int(sys.argv[1]) if len(sys.argv) > 1 else 3000
objs = [cls(i) for i in range(2) for cls in (A, B, C, D, E, F, G, H)]
t = 0
for i in range(n):
    t += entry(objs, i & 63)
print(t)
