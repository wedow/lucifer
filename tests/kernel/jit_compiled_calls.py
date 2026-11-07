# Calls compiled code makes to compiled functions it does not inline: a
# function and a bound method the caller only knows as arguments, one
# called without its defaulted parameters, and a recursion. tools/jitcheck runs this at two sizes and requires that no
# call per iteration enters compiled code from C (its direct calls): the
# caller pushes the callee's frame and enters its unit itself.
import sys

N = int(sys.argv[1]) if len(sys.argv) > 1 else 3000


def callee(a, b):
    return a + b


class K:
    def m(self, x):
        return x * 2


def defaulted(a, b=10, c=100):
    return a + b + c


def fib(n):
    return n if n < 2 else fib(n - 1) + fib(n - 2)


def hot(fn, bound, dfl, n):
    s = 0
    for i in range(n):
        s += fn(i, 1)
        s += bound(i)
        s += dfl(i)
        s += dfl(i, 2)
    return s


print(hot(callee, K().m, defaulted, N))
defaulted.__defaults__ = (20, 200)
print(hot(callee, K().m, defaulted, 3))
defaulted.__defaults__ = (30,)
try:
    hot(callee, K().m, defaulted, 3)
except TypeError as e:
    print('TypeError', e)
print(fib(15))
