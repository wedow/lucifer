# A hot loop reads and writes the attributes of instances of one class
# that got their names in different orders (pyperformance go's Square):
# some in __init__'s order, some later and out of it. tools/jitcheck runs
# this at two sizes (lane `attrorder`) and requires that guard and side
# exits, and entries left to the interpreter, do not grow with the
# iterations: every instance keeps its values in line at the offsets the
# class's sites read, whatever its order.
import sys

N = int(sys.argv[1]) if len(sys.argv) > 1 else 3000


class Square:
    def __init__(self, pos):
        self.pos = pos
        self.color = 0

    def link(self):
        self.ledges = 0
        self.reference = self

    def relink(self):
        self.reference = self
        self.ledges = 1


def step(s, k):
    s.color = s.color + k
    s.ledges = s.ledges + s.pos
    return s.reference is s


def hot(n):
    squares = [Square(i) for i in range(8)]
    for i, s in enumerate(squares):
        (s.link if i % 2 else s.relink)()
    total = 0
    for k in range(n):
        total += step(squares[k & 7], k & 3)
    return total, sum(s.color + s.ledges for s in squares)


print(hot(N))
print([list(vars(s)) for s in (Square(0), Square(1))])
s = Square(2)
s.relink()
print(list(vars(s)), hot(9))
