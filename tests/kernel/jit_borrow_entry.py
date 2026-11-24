# Loops entered in compiled code (OSR) while a local that a borrowed list or
# tuple read writes already holds a value: the entry value. Compiled code
# may stop counting that value only where a holder it can name keeps it
# (the container its reads come from, at the item the loop's last turn
# read); any other holder may let the value go while the loop still names
# it. Each shape gives the entry value another holder, or none, and then
# releases that holder: another local rebound to a number, a str or an
# object with a finalizer, deleted or augmented; a dict or an attribute;
# the container's slot overwritten by a callee before the entry; one value
# entering two locals; an entry at an outer loop; a generator's loop. The
# value must stay alive while a local names it and be freed exactly when
# CPython frees it: finalizers log when they run, and every result is
# printed after the loop left compiled code through a guard.
import gc
import weakref

N = 200000
K = N - 1000
LOG = []


class Fin:
    def __init__(self, tag):
        self.tag = tag

    def __del__(self):
        LOG.append(self.tag)


def report(name, got):
    print(name, got, sorted(LOG))
    LOG.clear()


# Two locals name the entry value; the loop lets the other one go. ys[0] is
# an int so that the unit compiled at the call leaves at once and the loop
# enters another; ys[K] is an int so that a guard leaves near the end with
# x in its state.
ALIAS = """
def f(xs, ys, w, z):
    s = make(w)
    x = "c"
    if w == 2:
        x = s
    i = 0
    acc = 0.0
    try:
        while True:
            if (i & 1) == w:
                x = xs[i]
            if i == z:
                RELEASE
            acc += ys[i]
            i += 1
    except IndexError:
        return x, acc
"""
RELEASES = {
    "number": "s = 5",
    "str": "s = 'q'",
    "finalizer": "s = Fin('s')",
    "del": "del s",
    "augmented": "s += AUG",
}
MAKES = {
    "int": (lambda w: 10 ** 30 + w, "1"),
    "str": (lambda w: "big" * 40 + str(w), "'z'"),
}


def aliased():
    xs = ["item%d" % k for k in range(N)]
    for kind, (make, aug) in MAKES.items():
        for how, stmt in RELEASES.items():
            ns = {"make": make, "Fin": Fin}
            exec(ALIAS.replace("RELEASE", stmt).replace("AUG", aug), ns)
            f = ns["f"]
            ys = [1.0] * N
            f(xs, ys, 0, 1)
            ys[K] = 1
            ys[0] = 0
            x, acc = f(xs, ys, 2, 2)
            report(f"alias {kind} {how}", (x == make(2), acc))


# Two carried values, one held by the list, one by its local only.
def two_entries():
    def f(xs, ys, w):
        x = xs[0]
        y = Fin("y") if w == 2 else xs[1]
        i = 0
        acc = 0.0
        try:
            while True:
                if (i & 1) == w:
                    x = xs[i]
                if (i & 1) == w:
                    y = xs[i]
                acc += ys[i]
                i += 1
        except IndexError:
            return x, y, acc

    xs = [Fin("x%d" % k) for k in range(N)]
    ys = [1.0] * N
    f(xs, ys, 0)
    ys[0] = 0
    first = xs[0]
    x, y, acc = f(xs, ys, 2)
    report("two entries", (x is first, y.tag, acc))
    del x, y
    gc.collect()
    report("two entries kept", (xs[0] is first, all(type(t) is Fin for t in xs)))
    del xs, first
    LOG.clear()


# The entry value held by its local alone, by a dict, or by an attribute;
# the loop rebinds the local from the list at turn z, and the other holder
# lets go at turn z + 1.
class Box:
    pass


def holders():
    def f(xs, ys, w, z, holder):
        x = Fin("only") if holder == "local" else (holder["k"] if type(holder) is dict else holder.k)
        r = weakref.ref(x)
        i = 0
        acc = 0.0
        try:
            while True:
                if i == z:
                    x = xs[i]           # CPython frees "only" here
                acc += ys[i]
                i += 1
        except IndexError:
            return x.tag, r() is None, acc

    xs = [Fin("i%d" % k) for k in range(N)]
    for kind in ("local", "dict", "attribute"):
        ys = [1.0] * N
        f(xs, ys, -1, -1, {"k": Fin("warm")})
        ys[K] = 1
        ys[0] = 0
        if kind == "dict":
            holder = {"k": Fin("dict")}
        elif kind == "attribute":
            holder = Box()
            holder.k = Fin("attribute")
        else:
            holder = kind
        report(f"holder {kind}", f(xs, ys, -1, N - 500, holder))
        del holder
        report(f"holder {kind} after", ())


# One value enters two locals; the list's slot is overwritten in the loop.
def two_webs():
    def f(xs, ys, z):
        x = y = xs[-1]
        i = 0
        acc = 0.0
        try:
            while True:
                if i == z:
                    x = xs[i]
                    xs[-1] = Fin("new")     # y alone keeps the old last item
                acc += ys[i]
                i += 1
        except IndexError:
            return x.tag, y.tag, acc

    for z in (-1, N - 2000):
        xs = [Fin("w%d" % k) for k in range(N)]
        ys = [1.0] * N
        ys[K] = 1
        ys[0] = 0
        report(f"two webs {z}", f(xs, ys, z))
        del xs
        report(f"two webs {z} after", ())


# An outer loop's entry: the inner loop's last item is the entry value.
# Between outer turns a callee overwrites that slot, so the local is its
# only holder when the loop enters compiled code.
def replace_last(xs, k):
    xs[-1] = Num("last%d" % k, 1)


def outer(rounds, xs, mutate):
    s = 0
    v = None
    for k in range(rounds):
        for v in xs:
            s += v.n
        if mutate and k % 7 == 3:
            replace_last(xs, k)
        if k == rounds - 2:
            LOG.append("turn")
    return s, v.tag


class Num(Fin):
    def __init__(self, tag, n):
        super().__init__(tag)
        self.n = n


def outer_loops():
    for mutate in (False, True):
        xs = [Num("n%d" % k, k) for k in range(100)]
        outer(50, xs, mutate)
        report(f"outer warm {mutate}", ())
        got = outer(3000, xs, mutate)
        report(f"outer {mutate}", got)
        del xs
        report(f"outer {mutate} after", ())


# Pairs unpacked in an outer loop's inner loop; the entry values are the
# last pair's items. The pairs list is replaced between calls.
def pairs(n, ps):
    s = 0
    a = b = None
    for _ in range(n):
        for a, b in ps:
            s += a.n - b.n
    return s, a.tag, b.tag


def tuple_entries():
    ps = [(Num("a%d" % k, k), Num("b%d" % k, 2 * k)) for k in range(50)]
    got = pairs(4000, ps)
    ps[-1] = (Num("A", 1), Num("B", 2))
    report("pairs", got)
    report("pairs again", pairs(4000, ps))
    del ps
    report("pairs after", ())


# A generator whose loop carries an item across its yields.
def gen(xs, n):
    for k in range(n):
        acc = 0
        for v in xs:
            acc += v.n
        yield acc, v.tag


def generators():
    xs = [Num("g%d" % k, k) for k in range(50)]
    out = []
    for got in gen(xs, 4000):
        out.append(got)
        if len(out) == 2000:
            xs[-1] = Num("G", 0)
    report("generator", (out[0], out[1999], out[2000], out[-1]))
    del xs
    report("generator after", ())


aliased()
two_entries()
holders()
two_webs()
outer_loops()
tuple_entries()
generators()
