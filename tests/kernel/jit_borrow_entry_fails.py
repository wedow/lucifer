# A loop entered in compiled code (OSR) while a local that a borrowed list
# read writes holds a value no container holds: here x names a big int that
# only x and s hold. The entry cannot borrow x's value, so the unit counts
# it; the check that finds this counts as a failed guard, and after enough
# failures the loop is built to count x from its entry. tools/jitcheck
# (lane entryfails) requires that exits from compiled code stay flat as the loop
# grows: an entry that leaves at once without counting a failure would make
# every back edge enter and leave again.
import sys

N = int(sys.argv[1]) if len(sys.argv) > 1 else 3000


def f(xs, ys, w, z):
    s = 10 ** 30 + w
    x = "c"
    if w == 2:
        x = s
    i = 0
    acc = 0.0
    try:
        while True:
            acc += s & 1
            if (i & 1) == w:
                x = xs[i]               # a list item read: x's web
            if i >= z:
                s = 5                   # s lets the big int go: x holds it alone
            acc += ys[i]                # an int near the end: a guard leaves with x in its state
            i += 1
    except IndexError:
        return x, acc


n = max(N, 2000)
xs = ["item%d" % k for k in range(n)]
ys = [1.0] * n
f(xs, ys, 0, 1)
ys[n - 1000] = 1
ys[0] = 0                               # the unit compiled at the call leaves early: the loop enters another
x, acc = f(xs, ys, 2, 2)
print("x kept:", x == 10 ** 30 + 2, acc)
