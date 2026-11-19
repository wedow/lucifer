# Hot operators whose operands are of one of a few classes at each site:
# == and != between None and instances that keep object's equality (and
# between two such classes), + on two classes with their own __add__, and
# [], []= and `in` on two classes with their own __getitem__, __setitem__
# and __contains__.
# tools/jitcheck runs this at two sizes and requires that compiled code's
# helper calls do not grow with the iterations: each site takes the class
# it meets in line, with the operator's dispatch folded on it. Then each
# class changes what it compares, adds or answers, and a subclass
# overrides the reflected method: every result must be the interpreter's.
import sys

N = int(sys.argv[1]) if len(sys.argv) > 1 else 3000


class P:
    pass


class Q:
    pass


class V:
    def __init__(self, x):
        self.x = x

    def __add__(self, o):
        return self.x + o


class W:
    def __init__(self, x):
        self.x = x

    def __add__(self, o):
        return self.x * o


class S:
    def __init__(self):
        self.last = 0

    def __getitem__(self, i):
        return i + 1

    def __setitem__(self, i, v):
        self.last = v

    def __contains__(self, k):
        return k > 2


class T:
    def __init__(self):
        self.last = 0

    def __getitem__(self, i):
        return i * 2

    def __setitem__(self, i, v):
        self.last = -v

    def __contains__(self, k):
        return k < 2


def equal(xs, pairs, rounds):
    n = 0
    for _ in range(rounds):
        for x in xs:
            if None == x:
                n += 1
            if x != None:
                n += 2
        for x, y in pairs:
            if x == y:
                n += 3
    return n


def sends(vs, ss, rounds):
    n = 0
    for _ in range(rounds):
        for i, v in enumerate(vs):
            n += v + i
        for i, s in enumerate(ss):
            n += s[i]
            if i in s:
                n += 1
            s[i] = n
    return n


def run(rounds):
    p, q = P(), Q()
    xs = [p, None, q, p, None]
    pairs = [(p, q), (q, p), (p, p), (q, q)]
    vs = [V(1), W(2), V(3)]
    ss = [S(), T(), S()]
    return equal(xs, pairs, rounds) + sends(vs, ss, rounds)


print(run(N))


class R(P):
    def __eq__(self, o):                # a subclass's own reflection goes first
        return 'R'

    def __ne__(self, o):
        return 'not R'


def changes(rounds):
    p, q, r = P(), Q(), R()
    out = []
    for _ in range(rounds):
        s, t = S(), T()
        s[1] = 5
        t[1] = 5
        out = [None == p, p != None, p == q, q != p, p == r, r == p, p != r, None != r, r == None,
               V(1) + 2, W(2) + 3, s[4], t[4], 4 in s, 4 in t, s.last, t.last]
    return out


print(changes(3000))
P.__eq__ = lambda self, o: 'P eq'          # a class gains __eq__: a site that folded to identity must not
V.__add__ = lambda self, o: 'V add'
T.__getitem__ = lambda self, i: 'T item'
S.__setitem__ = lambda self, i, v: setattr(self, 'last', 'S set')
del S.__contains__                        # `in` falls back to __getitem__ iteration
print(changes(3000))
del P.__eq__
print(changes(3000))


def midway(rounds):
    """Q gains __eq__ and S a new __setitem__ while the loop runs."""
    q, s = Q(), S()
    xs = [q, None]
    n = 0
    for i in range(rounds):
        if i == rounds // 2:
            Q.__eq__ = lambda self, o: 7
            S.__setitem__ = lambda self, i, v: setattr(self, 'last', v * 3)
        for x in xs:
            r = None == x
            n += r if type(r) is int else 10 + r
        s[0] = i
        n += s.last
    return n


print(midway(4000))
