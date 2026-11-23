# Hot reads of cells: free variables of a closure the call site learned
# (each call reads its own cells), of a closure held in a module global
# (a constant, whose cells are too), and a function's own cell variable
# read by the loop that a closure also reads. tools/jitcheck runs this at
# two sizes and requires that compiled code calls no helper per
# iteration: each read is a load of the cell's word, or, for a constant
# cell, a compare of its word with the value compiled code holds. After
# the loop, the cells change and the reads must see it.
import sys

N = int(sys.argv[1]) if len(sys.argv) > 1 else 3000


def mk(k, m):
    def add(v):
        return v * m + k
    return add


fixed = mk(7, 2)


def hot(n, f):
    base = 3
    peek = lambda: base
    s = 0
    for j in range(n):
        s += f(j) + fixed(j) + base + peek()
    return s


f = mk(1, 3)
print(hot(N, f))
print(hot(N, mk(2, 5)))
fixed.__closure__[0].cell_contents = 100
print(hot(3, f))
