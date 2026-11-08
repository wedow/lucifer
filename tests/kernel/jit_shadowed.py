# A polymorphic method site where some instances shadow the method with an
# attribute of their own (isort's Config objects did). The site's dict
# check keeps failing; once the site is marked, those instances take the
# generic path instead of leaving, and the code stops recompiling.
# tools/jitcheck runs this at two sizes and requires that compiles and
# guard exits do not grow with the iterations.
import sys

N = int(sys.argv[1]) if len(sys.argv) > 1 else 3000


class A:
    def __init__(self):
        self.x = 1

    def f(self):
        return self.x


class B:
    def __init__(self):
        self.y = 2

    def f(self):
        return self.y


def run(n):
    plain = [A(), B(), A(), B()]
    odd = A()
    odd.f = lambda: 5
    objs = plain + [odd]
    s = 0
    for i in range(n):
        for o in objs:
            s += o.f()
    return s


print(run(N))
