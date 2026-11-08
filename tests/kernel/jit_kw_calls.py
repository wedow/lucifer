# Calls with keywords of functions, methods and classes the caller knows:
# keywords that name positional parameters out of order, defaults left
# out between them, keyword-only parameters given or defaulted, and an
# __init__ that takes keywords. tools/jitcheck runs this at two sizes and
# requires that no call per iteration is made as C makes a keyword call
# (call_kw): each binds at compile time and inlines. After the loop, the
# defaults change under the compiled calls, and calls that cannot bind
# raise the interpreter's errors.
import sys

N = int(sys.argv[1]) if len(sys.argv) > 1 else 3000


def area(w, h=2, depth=1):
    return w * h * depth


def scaled(x, *, by=10, plus=0):
    return x * by + plus


class Point:
    def __init__(self, x, y=0, *, z=0):
        self.x = x
        self.y = y
        self.z = z

    def moved(self, dx=0, dy=0):
        return Point(self.x + dx, y=self.y + dy, z=self.z)


def hot(n):
    s = 0
    p = Point(0)
    for i in range(n):
        s += area(i, depth=3) + area(h=i, w=2) + area(i, 2, depth=i)
        s += scaled(i, plus=1) + scaled(i) + scaled(x=i, by=2)
        q = Point(i, z=1)
        p = p.moved(dy=1)
        s += q.x + q.z + Point(x=i, y=2).y
    return s + p.y


print(hot(N))
area.__defaults__ = (5, 7)
scaled.__kwdefaults__ = {'by': 3, 'plus': 4}
print(hot(3))
for call in (lambda: area(1, w=2), lambda: area(h=1), lambda: scaled(1, z=2), lambda: Point(1, w=0),
             lambda: Point(y=1), lambda: Point(1).moved(dz=1)):
    try:
        print(call())
    except TypeError as e:
        print('TypeError', e)
