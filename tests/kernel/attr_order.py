# Instances whose attributes are first assigned in different orders, as
# AWFY-style and pyperformance go's Square objects are (a name that one
# instance gets in __init__ another gets later, or a method sets names in
# another order): each instance's __dict__ keeps its own insertion order,
# deletion and re-assignment move a name to the end, and float and int
# values stored by a hot loop read back as stored. tools/jitcheck runs
# this at two sizes (lane `order`): the instances keep their values in
# line, so the hot loop's attribute reads take no generic path.
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
        self.used = True


def hot(n):
    squares = [Square(i) for i in range(8)]
    for i, s in enumerate(squares):
        (s.link if i % 2 else s.relink)()
    total = 0
    for k in range(n):
        s = squares[k % 8]
        total += s.pos + s.ledges + s.color + (s.reference is s)
        s.color = k * 0.5 if k % 3 else k
        s.ledges = s.ledges + 1
    return total, [type(s.color).__name__ for s in squares]


print(hot(N))
a, b = Square(1), Square(2)
a.relink()
b.link()
b.used = False
print(list(vars(a)), list(vars(b)))
del a.ledges
a.ledges = 9
print(list(a.__dict__), a.ledges, a.used)
c = Square(3)
c.used = 1.5
c.ledges = 2
c.reference = None
c.extra = 'x'
print(vars(c))
d = Square(4)
d.reference = d
del d.color
d.color = 7
print(list(vars(d)), d.color, d.reference is d)
import copy, pickle
e = copy.copy(c)
print(vars(e) == vars(c), list(vars(e)))
print(list(vars(pickle.loads(pickle.dumps(c)))))
print(hot(5))
