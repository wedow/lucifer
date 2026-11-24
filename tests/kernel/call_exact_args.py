# Calls of Python functions with exactly their positional parameters, and
# the neighbours that must take the general call: the callable changes
# kind, count and shape mid-loop, frames outgrow a stack chunk, recursion
# runs out, a function's code is replaced, and a tracer watches.
import sys

def add(a, b):
    return a + b

def closure(k):
    def inner(x):
        return x * k
    return inner

def gen(n):
    for i in range(n):
        yield i

def defaults(a, b=10):
    return a - b

def kwonly(a, *, b=3):
    return a * b

def star(*args):
    return len(args)

class C:
    def __init__(self, v):
        self.v = v
    def m(self, x):
        return self.v + x
    @staticmethod
    def s(x):
        return -x
    @classmethod
    def c(cls, x):
        return cls.__name__ + str(x)

c = C(5)
callables = [add, closure(3), gen, defaults, kwonly, star, c.m, C.s, C.c, len, C, max]
for i in range(60):
    f = callables[i % len(callables)]
    try:
        r = f(i) if f not in (add, max) else f(i, 2)
        print(i, list(r) if f is gen else r.v if f is C else r)
    except TypeError as e:
        print(i, "TypeError", e)

# The same site sees a function, then its bound method, then a builtin.
def site(g, x):
    return g(x)
for g in (abs, closure(2), c.m, abs, C.s):
    print(sum(site(g, x) for x in range(-3, 4)))

# Deep recursion through exact-arguments calls: many stack chunks, then the limit.
def depth(n, a, b, c, d, e, f, g, h):
    if n == 0:
        return 0
    return 1 + depth(n - 1, a, b, c, d, e, f, g, h)
print(depth(800, *range(8)))
sys.setrecursionlimit(300)
try:
    depth(1000, *range(8))
except RecursionError as e:
    print("RecursionError", e)
sys.setrecursionlimit(1000)
print(depth(500, *range(8)))

# A function whose code changes between calls at the same site.
def two(a, b):
    return a * b
def three(a, b, c):
    return a + b + c
def call2(f):
    try:
        return f(4, 5)
    except TypeError as e:
        return "TypeError " + str(e)
f = lambda a, b: a ** b
for i in range(6):
    if i == 3:
        f.__code__ = three.__code__
    print(call2(f))

# Exceptions raised in the callee unwind to the caller's handler.
def boom(x):
    if x % 3 == 0:
        raise ValueError(x)
    return x
t = 0
for i in range(20):
    try:
        t += boom(i)
    except ValueError as e:
        t -= e.args[0]
print(t)

# A tracer sees every call and return.
events = []
def tracer(frame, event, arg):
    if event in ("call", "return"):
        events.append((event, frame.f_code.co_name))
    return tracer
sys.settrace(tracer)
add(1, 2)
c.m(3)
sys.settrace(None)
print(events)
