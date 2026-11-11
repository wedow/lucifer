# Powers of a float and a bool (2.0 ** True, True ** 2.0) in a hot loop:
# the site's operand kinds are a float and an object, which the tier must
# not guard as an int on every run. tools/jitcheck runs this at two sizes
# and requires that guard and side exits do not grow with the iterations.
# Each result must be the interpreter's.
import sys

N = int(sys.argv[1]) if len(sys.argv) > 1 else 3000


def powers(n, flags):
    s = 0.0
    for i in range(n):
        b = flags[i & 1]
        s += 2.0 ** b + b ** 2.0 + (i * 0.5) ** True
    return s


print(powers(N, [True, False]))
