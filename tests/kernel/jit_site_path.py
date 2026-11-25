# luc-szyb: one helper in line under two parents, on a path the root takes
# on every entry and on one it takes on one entry in fifty. The helper's
# rare arm takes the inlining allowance ahead of its call of leaf, so leaf
# is left out of line in both copies. The call is hot only in the copy on
# the hot path: a purchase takes that copy in line and not the other.
# tools/jitcheck runs this with the tier's event log and requires that the
# root's rebuilds for its sites add at most one copy of leaf.
import sys

exec("def big(x, k):\n    s = x\n"
     + "".join(f"    s = (s * {31 + i} + k) & 0xffffff\n" for i in range(240))
     + "    return s\n")
exec("def leaf(x):\n    s = x\n"
     + "".join(f"    s = (s * {7 + i} + 3) & 0xffff\n" for i in range(100))
     + "    return s\n")


def helper(x, k):
    s = 0
    if k:
        s += big(x, 1) + big(x, 2)
    return s + leaf(x)


def via(x, k):
    return helper(x, k)


def hot_path(x, k):
    return via(x, k)


def cold_path(x, k):
    return via(x + 1, k)


def entry(x, k):
    s = 0
    if k:
        s += cold_path(x, k)
    return s + hot_path(x, k)


n = int(sys.argv[1]) if len(sys.argv) > 1 else 3000
t = 0
for i in range(n):
    t += entry(i, i % 50 == 7)
print(t)
