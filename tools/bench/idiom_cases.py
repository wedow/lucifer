"""Idioms for tools/bench/idioms: `RUNTIME idiom_cases.py NAME N` runs NAME(N) and prints its result.

Each function runs n outer iterations of about 100 inner ones of a single idiom and returns a value
every runtime must agree on. The list order is the table order; add idioms at the end of a group.
"""
import sys
from dataclasses import dataclass


class B:
    def __init__(self, i):
        self.x = i; self.y = i + 1; self.xv = 3; self.yv = 4; self.f = i * 0.5
    def get(self): return self.x
    def area(self): return self.x * 2
class C(B):
    def area(self): return self.y + 1
class D(B):
    def area(self): return self.xv - 1
class P:
    def __init__(self, x, y):
        self.x = x; self.y = y

def loads(n, bs):
    s = 0
    for _ in range(n):
        for b in bs:
            s += b.x + b.y + b.xv + b.yv
    return s
def stores(n, bs):
    for i in range(n):
        for b in bs:
            b.x = i; b.y = i
    return bs[0].x
def pstores(n, bs):
    o = bs[1]
    for i in range(n):
        for b in bs:
            b.q = o
    return 0
def fstores(n, bs):
    for i in range(n):
        for b in bs:
            b.f = b.f * 0.999 + 0.5
    return bs[0].f
def calls(n, bs):
    s = 0
    for _ in range(n):
        for b in bs:
            s += b.get()
    return s
def poly(n, bs):
    ms = [(B, C, D)[i % 3](i) for i in range(100)]
    s = 0
    for _ in range(n):
        for b in ms:
            s += b.area()
    return s
def alloc_local(n, bs):
    s = 0
    for i in range(n):
        for j in range(100):
            p = P(i, j)
            s += p.x + p.y
    return s
def alloc_escape(n, bs):
    out = [None] * 100
    for i in range(n):
        for j in range(100):
            out[j] = P(i, j)
    return out[5].x
def listsum(n, bs):
    xs = list(range(1000, 1100)); s = 0
    for _ in range(n):
        for v in xs:
            s += v
    return s
def listidx(n, bs):
    xs = list(range(1000, 1100)); s = 0
    for _ in range(n):
        for j in range(100):
            xs[j] = xs[j] + 1
    return xs[3]
def dictcount(n, bs):
    keys = ["k%d" % (i % 37) for i in range(100)]; d = {}
    for _ in range(n):
        for k in keys:
            d[k] = d.get(k, 0) + 1
    return d["k1"]
def dictint(n, bs):
    d = {i * 7: i for i in range(100)}; s = 0
    ks = list(d)
    for _ in range(n):
        for k in ks:
            s += d[k]
    return s
def strjoin(n, bs):
    parts = ["ab", "cde", "f", "ghij"]; s = 0
    for _ in range(n):
        for j in range(100):
            s += len("-".join(parts))
    return s
def strfmt(n, bs):
    s = 0
    for i in range(n):
        for j in range(100):
            s += len(f"{i}:{j}")
    return s
def strmeth(n, bs):
    ws = ["Hello World", "foo bar baz", " padded ", "x=1"] * 25; s = 0
    for _ in range(n):
        for w in ws:
            if w.startswith("foo"): s += 1
            s += len(w.strip()) + w.find("a")
    return s
def genexp(n, bs):
    xs = list(range(100)); s = 0
    for _ in range(n):
        s += sum(x * x for x in xs)
    return s
def listcomp(n, bs):
    xs = list(range(100)); s = 0
    for _ in range(n):
        s += len([x + 1 for x in xs if x & 1])
    return s
def tupunpack(n, bs):
    ps = [(i, i + 1) for i in range(100)]; s = 0
    for _ in range(n):
        for a, b in ps:
            s += a - b
    return s
def kwcall(n, bs):
    def f(a, b=2, *, c=3): return a + b + c
    s = 0
    for i in range(n):
        for j in range(100):
            s += f(j, c=i)
    return s
def starcall(n, bs):
    def g(a, b): return a + b
    def f(*args, **kw): return g(*args, **kw)
    s = 0
    for i in range(n):
        for j in range(100):
            s += f(j, b=i)
    return s
def tryexc(n, bs):
    d = {1: 2}; s = 0
    for i in range(n):
        for j in range(100):
            try:
                s += d[j & 1]
            except KeyError:
                s -= 1
    return s
def isinst(n, bs):
    s = 0
    for _ in range(n):
        for b in bs:
            if isinstance(b, (C, B)): s += 1
    return s
def floatloop(n, bs):
    x = 0.0
    for i in range(n):
        for j in range(100):
            x = x * 0.5 + j * 1.5
    return x
def closure(n, bs):
    def mk(k):
        def add(v): return v + k
        return add
    f = mk(3); s = 0
    for _ in range(n):
        for j in range(100):
            s += f(j)
    return s
def while_attr(n, bs):
    class Node:
        def __init__(self, nxt): self.next = nxt; self.v = 1
    head = None
    for _ in range(100): head = Node(head)
    s = 0
    for _ in range(n):
        p = head
        while p is not None:
            s += p.v; p = p.next
    return s


# --- attributes ---------------------------------------------------------------
class S:
    __slots__ = ("x", "y")
    def __init__(self, x, y):
        self.x = x; self.y = y
@dataclass
class DC:
    x: int
    y: int
class Prop:
    def __init__(self, v): self._v = v
    @property
    def v(self): return self._v
    @v.setter
    def v(self, value): self._v = value

def slots_rw(n, bs):
    ss = [S(i, i) for i in range(100)]
    for i in range(n):
        for o in ss:
            o.x = o.y + i
    return ss[7].x
def dataclass_read(n, bs):
    ds = [DC(i, 2 * i) for i in range(100)]; s = 0
    for _ in range(n):
        for o in ds:
            s += o.x - o.y
    return s
def property_rw(n, bs):
    ps = [Prop(i) for i in range(100)]
    for _ in range(n):
        for o in ps:
            o.v = o.v + 1
    return ps[9].v

# --- calls --------------------------------------------------------------------
class M:
    k = 3
    @staticmethod
    def st(a): return a + 1
    @classmethod
    def cm(cls, a): return a + cls.k
    def __init__(self): self.base = 2
    def add(self, a): return a + self.base
class Base:
    def val(self, a): return a + 1
class Sub(Base):
    def val(self, a): return super().val(a) * 2

def staticcall(n, bs):
    s = 0
    for _ in range(n):
        for j in range(100):
            s += M.st(j)
    return s
def classcall(n, bs):
    s = 0
    for _ in range(n):
        for j in range(100):
            s += M.cm(j)
    return s
def boundvar(n, bs):
    f = M().add; s = 0
    for _ in range(n):
        for j in range(100):
            s += f(j)
    return s
def lambdacall(n, bs):
    f = lambda a: a * 2; s = 0
    for _ in range(n):
        for j in range(100):
            s += f(j)
    return s
def defaultcall(n, bs):
    def f(a, b=5): return a - b
    s = 0
    for _ in range(n):
        for j in range(100):
            s += f(j)
    return s
def varargs(n, bs):
    def f(*a, **k): return len(a) + len(k)
    s = 0
    for _ in range(n):
        for j in range(100):
            s += f(j, j, z=j)
    return s
def supercall(n, bs):
    o = Sub(); s = 0
    for _ in range(n):
        for j in range(100):
            s += o.val(j)
    return s

# --- allocation ---------------------------------------------------------------
class KW:
    def __init__(self, a, b=0, *, c=1):
        self.a = a; self.b = b; self.c = c

def alloc_kwinit(n, bs):
    s = 0
    for i in range(n):
        for j in range(100):
            s += KW(j, c=i).c
    return s
def alloc_dropped(n, bs):
    for i in range(n):
        for j in range(100):
            S(i, j)
    return 0

# --- iteration ----------------------------------------------------------------
def for_tuple(n, bs):
    xs = tuple(range(100)); s = 0
    for _ in range(n):
        for v in xs:
            s += v
    return s
def for_dict(n, bs):
    d = {i: i for i in range(100)}; s = 0
    for _ in range(n):
        for k in d:
            s += k
    return s
def for_items(n, bs):
    d = {i: 2 * i for i in range(100)}; s = 0
    for _ in range(n):
        for k, v in d.items():
            s += k + v
    return s
def for_set(n, bs):
    xs = set(range(100)); s = 0
    for _ in range(n):
        for v in xs:
            s += v
    return s
def for_str(n, bs):
    t = "abcdefghij" * 10; s = 0
    for _ in range(n):
        for c in t:
            if c == "e": s += 1
    return s
def for_range(n, bs):
    s = 0
    for _ in range(n):
        for j in range(100):
            s += j
    return s
def for_enumerate(n, bs):
    xs = list(range(100)); s = 0
    for _ in range(n):
        for i, v in enumerate(xs):
            s += i * v
    return s
def for_zip(n, bs):
    xs = list(range(100)); ys = xs[::-1]; s = 0
    for _ in range(n):
        for a, b in zip(xs, ys):
            s += a * b
    return s
def for_reversed(n, bs):
    xs = list(range(100)); s = 0
    for _ in range(n):
        for v in reversed(xs):
            s = s * 3 + v & 0xffff
    return s
def while_index(n, bs):
    xs = list(range(100)); s = 0
    for _ in range(n):
        i = 0
        while i < 100:
            s += xs[i]; i += 1
    return s

# --- lists --------------------------------------------------------------------
def list_appendpop(n, bs):
    xs = []; s = 0
    for _ in range(n):
        for j in range(100):
            xs.append(j)
        for j in range(100):
            s += xs.pop()
    return s
def list_index(n, bs):
    xs = list(range(10)); s = 0
    for _ in range(n):
        for j in range(100):
            s += xs.index(j % 10)
    return s
def list_slice(n, bs):
    xs = list(range(20)); s = 0
    for _ in range(n):
        for j in range(100):
            s += len(xs[2:12])
    return s
def list_sort(n, bs):
    xs = [(i * 37) % 101 for i in range(100)]; s = 0
    for _ in range(n):
        s += sorted(xs)[50]
    return s
def list_in(n, bs):
    xs = list(range(8)); s = 0
    for _ in range(n):
        for j in range(100):
            if j & 15 in xs: s += 1
    return s

# --- dicts and sets -----------------------------------------------------------
def dict_strset(n, bs):
    keys = ["k%d" % i for i in range(100)]; d = {}
    for i in range(n):
        for k in keys:
            d[k] = i
    return d["k5"]
def dict_in(n, bs):
    d = {i: i for i in range(0, 100, 2)}; s = 0
    for _ in range(n):
        for j in range(100):
            if j in d: s += 1
    return s
def dict_tuplekey(n, bs):
    d = {(i, i + 1): i for i in range(100)}; s = 0
    ks = list(d)
    for _ in range(n):
        for k in ks:
            s += d[k]
    return s
def dict_update(n, bs):
    small = {"a": 1, "b": 2}; s = 0
    for _ in range(n):
        for j in range(100):
            d = {"c": j}; d.update(small); s += len(d)
    return s
def dict_setdefault(n, bs):
    d = {}; s = 0
    for _ in range(n):
        for j in range(100):
            s += len(d.setdefault(j & 7, []))
    return s
def dict_pop(n, bs):
    d = {}; s = 0
    for _ in range(n):
        for j in range(100):
            d[j] = j
        for j in range(100):
            s += d.pop(j)
    return s
def set_addin(n, bs):
    s = 0
    for _ in range(n):
        seen = set()
        for j in range(100):
            if j & 31 not in seen: seen.add(j & 31); s += 1
    return s

# --- str and bytes ------------------------------------------------------------
def str_concat(n, bs):
    s = 0
    for _ in range(n):
        t = ""
        for j in range(100):
            t += "x"
        s += len(t)
    return s
def str_format(n, bs):
    s = 0
    for i in range(n):
        for j in range(100):
            s += len("%d-%s" % (j, "ab"))
    return s
def str_split(n, bs):
    t = "alpha beta gamma delta"; s = 0
    for _ in range(n):
        for j in range(100):
            s += len(t.split())
    return s
def str_replace(n, bs):
    t = "hello world"; s = 0
    for _ in range(n):
        for j in range(100):
            s += len(t.replace("o", "00"))
    return s
def str_slice(n, bs):
    t = "abcdefghijklmnop"; s = 0
    for _ in range(n):
        for j in range(100):
            s += ord(t[3:9][2])
    return s
def str_in(n, bs):
    t = "the quick brown fox"; s = 0
    for _ in range(n):
        for j in range(100):
            if "fox" in t: s += 1
    return s
def str_encode(n, bs):
    t = "hello"; s = 0
    for _ in range(n):
        for j in range(100):
            s += len(t.encode())
    return s
def bytes_index(n, bs):
    b = bytes(range(100)); s = 0
    for _ in range(n):
        for j in range(100):
            s += b[j]
    return s

# --- numbers and tests ----------------------------------------------------------
def int_divmod(n, bs):
    s = 0
    for i in range(n):
        for j in range(1, 101):
            s += (i + j) // j + (i + j) % j
    return s
def int_pow(n, bs):
    s = 0
    for _ in range(n):
        for j in range(100):
            s += (j & 15) ** 3
    return s
def mixed_arith(n, bs):
    x = 0.0
    for _ in range(n):
        for j in range(100):
            x = x * 0.5 + j
    return x
def bigint(n, bs):
    big = 1 << 100; s = 0
    for _ in range(n):
        for j in range(100):
            s = (s + big * j) & ((1 << 120) - 1)
    return s
def compare_chain(n, bs):
    s = 0
    for _ in range(n):
        for j in range(100):
            if 10 < j <= 60 and (j & 1 or j == 20): s += 1
    return s

# --- tuples, generators, comprehensions ---------------------------------------
def tuple_build(n, bs):
    s = 0
    for _ in range(n):
        for j in range(100):
            a, b = (j, j + 1)
            a, b = b, a
            s += a - b
    return s
def genfunc(n, bs):
    def gen(k):
        for i in range(k):
            yield i
    s = 0
    for _ in range(n):
        for v in gen(100):
            s += v
    return s
def dictcomp(n, bs):
    xs = list(range(100)); s = 0
    for _ in range(n):
        s += len({x: x for x in xs})
    return s
def setcomp(n, bs):
    xs = list(range(100)); s = 0
    for _ in range(n):
        s += len({x & 15 for x in xs})
    return s

# --- control flow ---------------------------------------------------------------
class Ctx:
    def __enter__(self): return self
    def __exit__(self, *a): return False

def try_noraise(n, bs):
    s = 0
    for _ in range(n):
        for j in range(100):
            try:
                s += j
            except ValueError:
                s -= 1
    return s
def raise_catch(n, bs):
    s = 0
    for _ in range(n):
        for j in range(100):
            try:
                raise ValueError
            except ValueError:
                s += 1
    return s
def with_stmt(n, bs):
    c = Ctx(); s = 0
    for _ in range(n):
        for j in range(100):
            with c:
                s += j
    return s

# --- introspection and builtins -----------------------------------------------
def type_is(n, bs):
    s = 0
    for _ in range(n):
        for b in bs:
            if type(b) is B: s += 1
    return s
def getattr_const(n, bs):
    s = 0
    for _ in range(n):
        for b in bs:
            s += getattr(b, "x")
    return s
def hasattr_const(n, bs):
    s = 0
    for _ in range(n):
        for b in bs:
            if hasattr(b, "q"): s += 1
    return s
def builtins_small(n, bs):
    xs = [3, -1, 4]; s = 0
    for _ in range(n):
        for j in range(100):
            s += len(xs) + min(xs) + max(xs) + sum(xs) + abs(j - 50)
    return s
def anyall(n, bs):
    xs = [1, 2, 0, 3]; s = 0
    for _ in range(n):
        for j in range(100):
            if any(xs): s += 1
            if all(xs): s += 1
    return s

# --- scopes -------------------------------------------------------------------
G = 0
def global_rw(n, bs):
    global G
    for _ in range(n):
        for j in range(100):
            G = G + j
    return G
def nonlocal_rw(n, bs):
    t = 0
    def bump(j):
        nonlocal t
        t += j
    for _ in range(n):
        for j in range(100):
            bump(j)
    return t


def names():
    return [k for k, v in globals().items()
            if callable(v) and getattr(v, "__module__", None) == __name__
            and not isinstance(v, type) and k != "names"]


if __name__ == "__main__":
    if sys.argv[1] == "--list":
        print(" ".join(names()))
    else:
        name, n = sys.argv[1], int(sys.argv[2])
        print(globals()[name](n, [B(i) for i in range(100)]))
