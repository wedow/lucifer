# luc-1kz0: a function entered at its start on every iteration, whose calls
# out of line (on every entry, and in the loops of a rare arm) buy a larger
# inlining budget for that entry; a second rare arm, run only in the second half (once the
# start has grown), runs loops the interpreter enters a few times each
# (their own entries). A doubling is bought by the losses of the entry that made the
# calls: an entry at one of the loops starts at the base budget.
# tools/jitcheck runs this with the tier's event log and requires that
# entry's start was built grown and that no loop entry of it was.
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


def entry(x, k):
    s = a(x) + b(x) + c(x) + d(x) + e(x) + f(x) + g(x) + h(x)
    if k == 1:
        for j in range(300):
            s += a(j) - b(x)
        for j in range(300):
            s ^= c(j) + d(x)
    elif k == 2:
        for j in range(300):
            s += e(j) - f(x)
        for j in range(300):
            s ^= g(j) + h(x)
    return s


# The loop is module code, which stays in the interpreter: entry is its own unit's root.
n = int(sys.argv[1]) if len(sys.argv) > 1 else 3000
t = 0
for i in range(n):
    t += entry(i, 1 if i % 97 == 5 else 2 if i >= n // 2 and i % 89 == 0 else 0)
print(t)
