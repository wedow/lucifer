# A rebound global's old value is freed at the rebind, as in CPython, though
# compiled code read it as a constant; a value a running call still holds
# lives until that call is done with it.


class Foo:
    def __init__(self, n):
        self.n = n

    def __del__(self):
        print("del", self.n)


def h():
    return r is None


for i in range(4):
    r = Foo(i)
    h()
    print("rebind", i)

# Called often enough to be compiled again after each rebind.
for i in range(4, 8):
    r = Foo(i)
    for _ in range(50):
        h()
    print("rebind", i)


def g():
    global r
    r = Foo(100 + i)
    return 1


def show(x, y):
    print("show", x.n, y)


def k():
    show(r, g())


for i in range(3):
    r = Foo(10 + i)
    k()
    print("after", i)


def loop():
    global r
    for j in range(3):
        r = Foo(20 + j)
        h()
        print("loop", j)


loop()
r = None
print("end")
