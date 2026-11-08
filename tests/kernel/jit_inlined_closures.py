# Methods that make closures (MAKE_FUNCTION, and MAKE_CELL for the values
# they capture), as DeltaBlue's Plan.execute passes a lambda to for_each,
# called from a hot loop that inlines them. tools/jitcheck runs this at two
# sizes and requires that no call per iteration enters compiled code
# (entries, direct and compiled calls): the closures are made in the
# inlined frames and inline where they are called. A closure that
# escapes, and one whose cell changes after it is made, must be the
# interpreter's.
import sys

N = int(sys.argv[1]) if len(sys.argv) > 1 else 3000


class Plan:
    def __init__(self, items):
        self.items = items

    def for_each(self, fn):
        s = 0
        for x in self.items:
            s += fn(x)
        return s

    def execute(self, k):
        return self.for_each(lambda x: x * k)

    def shifted(self, k):
        k += 1

        def add(x):
            return x + k
        return add


def hot(n):
    p = Plan([1, 2, 3])
    s = 0
    for i in range(n):
        s += p.execute(i) + p.shifted(i)(1)
    return s


print(hot(N))
p = Plan([4])
f = p.shifted(5)
print(f(1), f.__closure__[0].cell_contents, f.__qualname__, p.execute(2))
