# luc-szyb: a helper whose own unit buys a large callee it calls on every
# call (its rare arm takes its inlining allowance first). In the second
# half the helper is called from a caller, within the caller's allowance,
# that never bought the callee: the caller's build must not take the
# callee in line with it, since nothing the caller counted paid for it.
# tools/jitcheck runs this with the tier's event log and requires that no
# build of the caller is as large as the helper's build that took the
# callee in.
import sys

exec("def big(x, k):\n    s = x\n"
     + "".join(f"    s = (s * {31 + i} + k) & 0xffffff\n" for i in range(240))
     + "    return s\n")
exec("def large(x):\n    s = x\n"
     + "".join(f"    s = (s * {7 + i} + 3) & 0xffff\n" for i in range(200))
     + "    return s\n")


def helper(x, k):
    s = 0
    if k:
        s += big(x, 1) + big(x, 2)
    return s + large(x)


def caller(x, k):
    return helper(x, k) + 1


n = int(sys.argv[1]) if len(sys.argv) > 1 else 3000
t = 0
for i in range(n):
    k = i % 50 == 7
    t += helper(i, k) if i < n // 2 else caller(i, k)
print(t)
