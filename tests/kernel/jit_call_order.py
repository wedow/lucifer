# luc-sl4y: a loop calling operator.add through a local after another loop
# called it through a class attribute with other operand types. Compiled
# code inlines the callee at each site with that site's operand types, in
# either order (argv[2], "ab" or "ba"): tools/jitcheck runs both at two
# sizes and requires that entries into compiled code and calls compiled
# code makes do not grow with the iterations.
import math
import operator
import sys

N = int(sys.argv[1]) if len(sys.argv) > 1 else 3000
ORDER = sys.argv[2] if len(sys.argv) > 2 else 'ab'


class K:
    add = operator.add


def through_attr(n):
    s = 0
    for i in range(n):
        s = K.add(s, i)
    return s


def through_local(n):
    add = operator.add
    sqrt = math.sqrt
    s = 0.0
    for i in range(n):
        s = add(s, sqrt(i))
    return s


for k in ORDER:
    print(k, through_attr(N) if k == 'a' else through_local(N))
