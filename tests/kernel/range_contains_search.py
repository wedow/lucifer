# `x in range` for a probe that is not an exact int stops at the first equal item.


class TrueAt:
    def __init__(self, m):
        self.m, self.calls = m, 0

    def __eq__(self, o):
        n = self.calls
        self.calls += 1
        return n == self.m


class AlwaysEq(int):
    calls = 0

    def __eq__(self, o):
        self.calls += 1
        return True
    __hash__ = int.__hash__


for r, p in [(range(0, -14, -2), TrueAt(0)), (range(1, 39, 2), AlwaysEq(39)), (range(5), TrueAt(3)),
             (range(4), TrueAt(9))]:
    print(p in r, p.calls)
print(AlwaysEq(5) in range(-2**63 - 1, 2**64 + 2, 2))
print(3.0 in range(5), 2.5 in range(5), True in range(2), 'a' in range(3))
print(range(10).index(AlwaysEq(7)), range(10).count(AlwaysEq(7)))
