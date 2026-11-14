# A reference count read while compiled code runs is the count the
# interpreter's frames would give: one per local and per stack entry that
# names the object, in the frame that reads it and in its callers. Each
# shape is read again and again, as the interpreter runs it and as
# compiled code does (and as that leaves at failing guards), and must
# read the same every time.
import sys

grc = sys.getrefcount


class C:
    pass


def one():
    x = C()
    return grc(x)


def two():
    x = C()
    y = x
    return grc(x)


def three():
    x = C()
    y = x
    z = y
    return grc(z), grc(x)


def arg(x):
    return grc(x)


def passed():
    x = C()
    return arg(x)


def passed_twice():
    x = C()
    y = x
    return arg(x), arg(y)


def passed_kw():
    x = C()
    return arg(x=x)


def deeper():
    x = C()
    return (x, grc(x))[1]


def nested():
    x = C()
    return max(grc(x), arg(x))


def temp():
    return grc(C())


def looped():
    x = C()
    seen = []
    for i in range(4):
        y = x
        seen.append(grc(x))
    return seen


def attribute():
    o = C()
    o.a = C()
    return grc(o.a), grc(o)


def method():
    o = C()
    return o.__class__, grc(o)


class Reader:
    def read(self, x):
        return grc(x), grc(self)


def through_method():
    r = Reader()
    x = C()
    return r.read(x)


def rebound(n):
    x = C()
    before = grc(x)
    for i in range(n):
        y = x
        y = None
    return grc(x) == before


shapes = (one, two, three, passed, passed_twice, passed_kw, deeper, nested, temp, looped,
          attribute, through_method)
for shape in shapes:
    seen = []
    for turn in range(400):
        r = shape()
        if isinstance(r, tuple) and isinstance(r[0], type):
            r = r[1:]
        if r not in seen:
            seen.append(r)
    print(shape.__name__, len(seen))
print('method', len({method()[1] for turn in range(400)}))
print('rebound', [rebound(n) for n in (0, 1, 5, 300, 5, 1, 0)])
