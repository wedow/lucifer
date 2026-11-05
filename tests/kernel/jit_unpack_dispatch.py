# Two paths compiled code now takes instead of leaving: unpacking an exact
# tuple (other sequences, wrong sizes and tuple subclasses must still behave
# as the interpreter has them), and a special method found at run time and
# called by the kernel's operator dispatch (functions, kernel descriptors,
# and descriptors that bind through __get__ such as staticmethod and
# classmethod, NotImplemented and the reflected operand).
N = 3000


class T(tuple):
    pass


def unpack(items):
    total = 0
    for it in items:
        try:
            a, b = it
            total += a * 10 + b
        except ValueError as e:
            total += 1000
            last = str(e)
    return total, last


class V:
    def __init__(self, x):
        self.x = x

    def __add__(self, other):
        if isinstance(other, V):
            return V(self.x + other.x)
        return NotImplemented

    def __radd__(self, other):
        return V(self.x + other * 100)

    def __eq__(self, other):
        return isinstance(other, V) and self.x == other.x

    __hash__ = None


class S:
    @staticmethod
    def __add__(b):
        return ('static', b)

    @classmethod
    def __mul__(cls, b):
        return (cls.__name__, b)


def dispatch(n):
    v = V(0)
    out = []
    for i in range(n):
        v = v + V(1)
        if i % 500 == 0:
            out.append((1 + v).x)
            out.append(v == V(i + 1))
    s = S()
    out.append(s + 1)
    out.append(s * 2)
    try:
        out.append(V(1) + 'x')
    except TypeError as e:
        out.append(str(e))
    return v.x, out


items = [(i % 7, i % 3) for i in range(N)] + [[1, 2], T((3, 4)), (1, 2, 3), (5,)]
print(unpack(items))
print(dispatch(N))
