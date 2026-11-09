# A generator whose loop keeps values in registers across the calls and
# releases of its body and across its yields (luc-hez4): a pure-Python
# itertools.permutations, consumed by a generator that filters it, as
# pyperformance's nqueens runs it. Each output line must be the
# interpreter's.


def permutations(iterable, r=None):
    pool = tuple(iterable)
    n = len(pool)
    if r is None:
        r = n
    indices = list(range(n))
    cycles = list(range(n - r + 1, n + 1))[::-1]
    yield tuple(pool[i] for i in indices[:r])
    while n:
        for i in reversed(range(r)):
            cycles[i] -= 1
            if cycles[i] == 0:
                indices[i:] = indices[i + 1:] + indices[i:i + 1]
                cycles[i] = n - i
            else:
                j = cycles[i]
                indices[i], indices[-j] = indices[-j], indices[i]
                yield tuple(pool[i] for i in indices[:r])
                break
        else:
            return


def n_queens(queen_count):
    cols = range(queen_count)
    for vec in permutations(cols):
        if (queen_count == len(set(vec[i] + i for i in cols))
                == len(set(vec[i] - i for i in cols))):
            yield vec


for n in range(1, 8):
    solutions = list(n_queens(n))
    print(n, len(solutions), solutions[:2])
print(len(list(permutations(range(6), 3))), list(permutations('abc', 2)))
