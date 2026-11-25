# luc-szyb: a function entered on every iteration whose one hot call, made
# once per entry, comes after the calls of an arm it takes on one entry in
# fifty. The arm's calls come first in the code and take more than the
# inlining allowance would hold after four doublings, so a rule that spends
# the allowance in code order never reaches the hot call. Its counted calls
# buy it in line once they have paid the build. tools/jitcheck runs this at
# two sizes and requires that compiled calls do not grow by more than half
# a call an iteration between them.
import sys

# A long straight run of arithmetic: a large callee with no calls of its own.
exec("def big(x, k):\n    s = x\n"
     + "".join(f"    s = (s * {31 + i} + k) & 0xffffff\n" for i in range(240))
     + "    return s\n")


def hot(x):
    return (x * 7 + 3) & 255


def entry(x, k):
    s = 0
    if k:
        s += (big(x, 1) + big(x, 2) + big(x, 3) + big(x, 4) + big(x, 5) + big(x, 6) + big(x, 7)
              + big(x, 8) + big(x, 9) + big(x, 10) + big(x, 11) + big(x, 12) + big(x, 13)
              + big(x, 14) + big(x, 15) + big(x, 16) + big(x, 17) + big(x, 18) + big(x, 19)
              + big(x, 20))
    return s + hot(x)


# The loop is module code, which stays in the interpreter: entry is its own unit's root.
n = int(sys.argv[1]) if len(sys.argv) > 1 else 3000
t = 0
for i in range(n):
    t += entry(i, i % 50 == 7)
print(t)
