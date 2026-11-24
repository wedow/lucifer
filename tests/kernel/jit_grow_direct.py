# A large callee that compiled code enters directly (a call site with
# seven callees takes none in line), raising now and then from the direct
# entry, its unit killed and rebuilt while the loop keeps entering it.
# Values and errors stay as the interpreter's. (It was written for the
# fixed growth trigger, now gone: the callee's first build takes big in
# line, so it is not grown here.)
import sys

N = int(sys.argv[1]) if len(sys.argv) > 1 else 40000

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
