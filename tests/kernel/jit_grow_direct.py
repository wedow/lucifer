# A hot callee compiled code enters directly, which leaves a call out of
# line for want of budget: at its unit's HOT_ENTRIES-th direct entry the
# unit dies and its code is built again with more budget, while that
# entry's activation runs on in the dead unit. Values and errors stay as
# the interpreter's.
import sys

N = int(sys.argv[1]) if len(sys.argv) > 1 else 40000

# Too big to take in line at the first budget.
src = "def big(x):\n" + "".join(f"    x = (x * {i % 7 + 2} + {i}) % 1000003\n" for i in range(240)) + "    return x\n"
ns = {}
exec(src, ns)
big = ns["big"]


def mid(x):
    if x % 10007 == 10006:
        raise ValueError(x)
    return big(x) + big(x + 1)


def f1(x): return x + 1
def f2(x): return x * 2
def f3(x): return x - 3
def f4(x): return x ^ 5
def f5(x): return x // 2
def f6(x): return -x


def loop(n):
    # A site that meets seven callees takes none in line: mid is entered directly.
    fs = [mid, f1, f2, f3, f4, f5, f6]
    total = 0
    errors = 0
    for i in range(n):
        try:
            total += fs[i % 7](i)
        except ValueError:
            errors += 1
    return total, errors


print(loop(N))
