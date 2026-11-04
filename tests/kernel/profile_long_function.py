# The call events of sys.setprofile cost the same in a long function as in a
# short one: finding the calling instruction does not walk the code.
import sys
import time


def g():
    pass


def run(src):
    ns = {"g": g}
    exec(src, ns)
    f = ns["f"]
    f(10)
    sys.setprofile(lambda frame, event, arg: None)
    t0 = time.perf_counter()
    f(20000)
    t = time.perf_counter() - t0
    sys.setprofile(None)
    return t


body = "".join(f"    x{i} = {i}\n" for i in range(3000))
loop = "    for _ in range(k):\n        g()\n"
short = run("def f(k):\n" + loop)
long = run("def f(k):\n" + body + loop)
print(long < 10 * short)
