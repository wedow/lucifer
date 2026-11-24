# luc-1kz0: a function whose calls are more than its first build's budget
# takes in line, entered once every four iterations, each entry doing little
# work against what a larger build costs. The calls it makes out of line
# never cost what a build with twice the budget would: the unit is not
# built again larger. tools/jitcheck runs this at two sizes and requires
# that the IR instructions the tier builds do not grow with the iterations.
import sys


def part(x, k):
    s = 0
    if x & 1:
        s += x * k
    else:
        s -= x // (k + 1)
    if x % 3 == k % 3:
        s ^= k << 2
    t = (x + k) * (x - k)
    if t > 1000:
        s += t % 97
    elif t < -1000:
        s -= t % 89
    else:
        s += t
    s += (x & k) + ((x >> 1) & k)
    u = (s ^ x) % (k + 7)
    if u > 3:
        s += u * 3
    elif u == 2:
        s -= k
    else:
        s += x % 11
    v = (s + k) * 3 - x
    if v & 4:
        s += v // 5
    else:
        s -= v % 13
    return s


def a(x): return part(x, 1) + part(x + 1, 2)
def b(x): return part(x, 3) + part(x + 2, 4)
def c(x): return part(x, 5) + part(x + 3, 6)
def d(x): return part(x, 7) + part(x + 4, 8)
def e(x): return part(x, 9) + part(x + 5, 10)
def f(x): return part(x, 11) + part(x + 6, 12)
def g(x): return part(x, 13) + part(x + 7, 14)
def h(x): return part(x, 15) + part(x + 8, 16)


def entry(x):
    if x < 0:
        return 0
    return a(x) + b(x) + c(x) + d(x) + e(x) + f(x) + g(x) + h(x)


# The loop is module code, which stays in the interpreter: entry is its own unit's root.
t = 0
for i in range(int(sys.argv[1]) if len(sys.argv) > 1 else 3000):
    if i % 4 == 0:
        t += entry(i)
print(t)
