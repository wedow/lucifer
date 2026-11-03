# global, nonlocal, del, walrus and class scope.
counter = 0
items = []


def bump():
    global counter
    counter += 1
    return counter


def make_global():
    global created_later
    created_later = "made"


bump(); bump()
make_global()
print(counter, created_later)


def outer():
    n = 0
    log = []

    def inc():
        nonlocal n
        n += 1
        log.append(n)
        return n

    def reset():
        nonlocal n
        n = 100

    inc(); inc()
    reset()
    inc()
    return n, log


print(outer())


def three_levels():
    x = "outer"

    def middle():
        def inner():
            nonlocal x
            x = x + "+inner"
        inner()
        return x

    r = middle()
    return r, x


print(three_levels())


def deletes():
    a = 1
    b = [1, 2, 3]
    d = {"k": 1, "j": 2}
    del a
    try:
        print(a)
    except UnboundLocalError as e:
        print(type(e).__name__, e)
    del b[1]
    del d["k"]
    del b[0:1]
    print(b, d)
    class C: pass
    c = C()
    c.attr = 1
    del c.attr
    print(hasattr(c, "attr"))
    x = 5
    def peek():
        return x
    del x
    try:
        peek()
    except NameError as e:
        print(type(e).__name__, e)


deletes()

g1 = 1
del g1
try:
    print(g1)
except NameError as e:
    print(e)
try:
    del g1
except NameError as e:
    print(e)


def walrus():
    data = [1, 5, 8, 12]
    print([y for x in data if (y := x * 2) > 8], y)
    if (n := len(data)) > 3:
        print("long", n)
    while (m := data.pop()) > 5:
        print("popped", m)
    print(m, (a := 1, b := a + 1), a, b)
    total = 0
    print([total := total + v for v in data], total)
    return (z := 10) + z


print(walrus())
print((top := 7), top)


class Scope:
    x = "class"
    y = [x for _ in range(2)] if False else None
    items = [1, 2, 3]
    doubled = [i * 2 for i in items]
    lookup = {k: x for k in "ab"} if False else None

    def method(self):
        return x if False else self.x

    z = x + "!"
    temp = 1
    del temp

    def uses_global(self):
        return counter


print(Scope.z, Scope.doubled, Scope().method(), Scope().uses_global(), hasattr(Scope, "temp"))

x = "module"


class Shadow:
    x = "shadow"

    def get(self):
        return x

    gen = list(x for _ in range(1))
    lam = (lambda: x)()


print(Shadow().get(), Shadow.gen, Shadow.lam)


class Counter:
    count = 0

    def __init__(self):
        Counter.count += 1
        self.id = Counter.count


Counter(); Counter()
print(Counter.count, Counter().id)


def class_in_function():
    v = "closed"

    class Inner:
        w = v
        def get(self):
            return v

    return Inner.w, Inner().get()


print(class_in_function())


def unbound():
    try:
        print(late)
    except UnboundLocalError as e:
        print(e)
    late = 1
    return late


print(unbound())


def free_unbound():
    def get():
        return later
    try:
        get()
    except NameError as e:
        print(e)
    later = 1
    return get()


print(free_unbound())


def comp_scope():
    i = "outer"
    r = [i for i in range(3)]
    return i, r


print(comp_scope())
print(globals().get("counter"), "bump" in dir(), locals() is globals())
def nested_default(a, b=counter):
    return a, b
counter = 99
print(nested_default(1))
