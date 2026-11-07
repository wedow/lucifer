# A hot loop whose guards check what a writer can change (a global
# function and its code, a global constant, a class and its attribute)
# or what never does (the range iterator's type). They leave the loop, so
# its iterations run no guards (tools/jitcheck).
import sys


class Config:
    scale = 3


def double(x):
    return 2 * x


LIMIT = 7


def hot(n):
    total = 0
    for i in range(n):
        total += double(i) % LIMIT + Config.scale
    return total


print(hot(int(sys.argv[1]) if len(sys.argv) > 1 else 1000))
