# luc-szyb: a function entered on every iteration whose one hot call, made
# once per entry, is left out of line behind a rare arm's calls, and whose
# place for an object's method meets a new class in each of the first
# phases. A purchase of the hot call is a rebuild: made while the place is
# still learning, it is followed by a learned rebuild of the larger unit.
# Taken with the learned rebuild that happens anyway, the call costs no
# rebuild of its own. tools/jitcheck runs this with the tier's event log and
# requires that the function's start is never rebuilt for its sites alone
# while a learned rebuild follows.
import sys

exec("def big(x, k):\n    s = x\n"
     + "".join(f"    s = (s * {31 + i} + k) & 0xffffff\n" for i in range(240))
     + "    return s\n")


class A:
    def v(self, x): return x + 1
class B:
    def v(self, x): return x + 2
class C:
    def v(self, x): return x + 3
class D:
    def v(self, x): return x + 4


def hot(x):
    return (x * 7 + 3) & 255


def entry(x, o, k):
    s = o.v(x)
    if k:
        s += (big(x, 1) + big(x, 2) + big(x, 3) + big(x, 4) + big(x, 5) + big(x, 6) + big(x, 7)
              + big(x, 8) + big(x, 9) + big(x, 10))
    return s + hot(x)


n = int(sys.argv[1]) if len(sys.argv) > 1 else 3000
objs = [A(), B(), C(), D()]
t = 0
for i in range(n):
    kinds = 1 + min(3, i * 8 // n) if n >= 8 else 1
    t += entry(i, objs[i % kinds], i % 50 == 7)
print(t)
