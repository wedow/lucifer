# A hot loop reads the cell of a closure held in a module global (a
# constant to compiled code) while an extension writes the cell with
# PyCell_SET, which no runtime code sees: each read after the write must
# see the new value.
import cell_set


def mk(k):
    def add(v):
        return v + k
    return add


adder = mk(1)
cell = adder.__closure__[0]


def hot(n):
    s = 0
    for j in range(n):
        s += adder(j)
        if j % 1000 == 999:
            cell_set.set(cell, j)
    return s


for _ in range(3):
    print(hot(20000))
