# luc-j4mj: a function that leaves compiled code on every call for a while
# (unary plus, which the tier does not compile), then stops. Its
# entry goes back to the interpreter while it keeps leaving; after the
# phase change it must be compiled and entered again, not stay
# interpreted for the rest of the process. The same for a recursive
# function that leaves after its recursive call returns: once it is retired,
# the activations still running the retired unit leave too, and must not
# count as more retirements. tools/jitcheck runs this at two
# sizes and requires that calls of it that compiled code cannot enter
# (because its entry was left to the interpreter) do not grow with the
# iterations.
import sys

N = int(sys.argv[1]) if len(sys.argv) > 1 else 3000


def g(i, early):
    if early:
        return +i
    return i + 1


def run(n, early):
    s = 0
    for i in range(n):
        s += g(i, early)
    return s


def rec(d, early):
    v = rec(d - 1, early) if d else 0
    if early:
        return +v + 1
    return v + 1


def run_rec(n, early):
    s = 0
    for i in range(n // 50):
        s += rec(50, early)
    return s


print(run(2000, True), run(N, False))
print(run_rec(20000, True), run_rec(N * 5, False))
