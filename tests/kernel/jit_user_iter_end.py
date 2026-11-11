# Hot loops over iterators of user classes whose __next__ ends the
# iteration by raising StopIteration: the tier takes the step in line, and
# the raise leaves the inlined __next__ for the loop's end with its
# traceback entry made in place. tools/jitcheck runs this at two sizes and
# requires that exits and compiled calls do not grow with the iterations.
# Each result must be the interpreter's.
import sys

N = int(sys.argv[1]) if len(sys.argv) > 1 else 3000


class Count:
    def __init__(self, n):
        self.n = n

    def __iter__(self):
        return self

    def __next__(self):
        if self.n <= 0:
            raise StopIteration
        self.n -= 1
        return self.n


def total(n):
    s = 0
    for i in range(n):
        s += sum(Count(8))
        for v in Count(3):
            s += v
    return s


print(total(N))
