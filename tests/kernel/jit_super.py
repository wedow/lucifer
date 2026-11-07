# Constructors and methods that reach a base class through super(), with no
# arguments and with two, hot: compiled code takes super's attribute itself
# (LOAD_SUPER_ATTR). tools/jitcheck runs this at two sizes and requires that
# side exits do not grow and that none of these functions is retired.
import sys

N = int(sys.argv[1]) if len(sys.argv) > 1 else 3000


class Base:
    def __init__(self, v):
        self.v = v

    def get(self):
        return self.v


class Mid(Base):
    def __init__(self, v):
        super().__init__(v + 1)

    def get(self):
        return super().get() * 2


class Pair(Base):
    def get(self):
        return super(Pair, self).get() + 1


def hot(n):
    s = 0
    for i in range(n):
        s += Mid(i).get() + Pair(i).get()
    return s


print(hot(N))
