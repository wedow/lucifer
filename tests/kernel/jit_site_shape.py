# luc-szyb: a hot call, once per entry, to a function whose own unit buys a
# call of its own: both callers leave the calls of a rare arm ahead of
# their hot call, and those take the inlining allowance. When the root
# buys the call, the callee goes in line as its own unit is built: with the
# allowance it has there and the call its own unit bought, so neither call
# is left out of line. tools/jitcheck runs this at two sizes and requires
# that compiled calls do not grow by more than half a call an iteration
# between them.
import sys

exec("def big(x, k):\n    s = x\n"
     + "".join(f"    s = (s * {31 + i} + k) & 0xffffff\n" for i in range(240))
     + "    return s\n")


def leaf(x):
    return (x * 7 + 3) & 255


def mid(x, k):
    s = 0
    if k:
        s += big(x, 21) + big(x, 22) + big(x, 23) + big(x, 24)
    return s + leaf(x)


def entry(x, k):
    s = 0
    if k:
        s += big(x, 1) + big(x, 2) + big(x, 3) + big(x, 4)
    return s + mid(x, k)


n = int(sys.argv[1]) if len(sys.argv) > 1 else 3000
t = 0
for i in range(n):
    t += entry(i, i % 50 == 7)
print(t)
