# Builtins whose own code calls functions and primitives with exactly their
# positional parameters, and what must still see those calls: defaults and
# builtins changed mid-loop (also by another thread), recursion through
# builtins to the limit, a profiler switched on mid-run, frames and
# tracebacks seen from a callback a builtin calls, generators.
import builtins, sys, threading, traceback

def show(f, *args):
    try:
        print(f(*args))
    except Exception as e:
        print(type(e).__name__, e)

words = ["pear", "Fig", "apple", "kiwi", "Banana", "date"]
for i in range(40):
    w = words[i % len(words)]
    r = (w.upper(), w.find("a"), w.startswith("k"), w.strip("p"), w * 2, len(w), sorted(w),
         "-".join(words[: i % 4]), {w: i}.get(w), [w].index(w), str(i), repr(w), hash(w) == hash(w))
    if i % 7 == 0:
        print(i, r)
show(int, "x1")
show("abc".index, "z")
show([].pop)
show(sorted, [3, None, 1])
show(min, [])

# Defaults replaced mid-loop: the same site goes from exact to defaulted arguments and back.
def scale(x, k=2):
    return x * k
t = 0
for i in range(30):
    if i == 10:
        scale.__defaults__ = (5,)
    if i == 20:
        scale.__defaults__ = None
    try:
        t += scale(i) if i < 20 else scale(i, 3)
    except TypeError as e:
        print("TypeError", e)
print(t)

# A builtin shadowed by a global, then rebound in builtins, mid-loop: user
# code sees each, builtins' own code sees neither.
real_divmod = builtins.divmod
calls = []
def fake(*args):
    calls.append(args)
    return 42
for i in range(9):
    if i == 3:
        len = fake
    if i == 6:
        del len
        builtins.divmod = fake
    print(i, len("abc"), divmod(7, 2), sorted("cba"), "x".join("ab"), [1, 2, 3].index(3), (7).__divmod__(2))
builtins.divmod = real_divmod
print(len(calls))

# Another thread replaces a function's code and defaults between two halves of a loop.
def work(a, b):
    return a + b
def other(a, b=100):
    return a - b
go, done = threading.Event(), threading.Event()
def swap():
    go.wait()
    work.__code__ = other.__code__
    work.__defaults__ = (100,)
    done.set()
th = threading.Thread(target=swap)
th.start()
acc = []
for i in range(20):
    if i == 10:
        go.set()
        done.wait()
    acc.append(work(i, 1) if i < 10 or i % 2 else work(i))
th.join()
print(acc)

# Recursion through builtins: a repr that recurses runs out at the user limit.
class Node:
    def __init__(self, child):
        self.child = child
    def __repr__(self):
        return "N(" + repr(self.child) + ")"
    def __eq__(self, other):
        return isinstance(other, Node) and self.child == other.child
    __hash__ = object.__hash__
n = None
for i in range(200):
    n = Node(n)
print(len(repr(n)))
print(n == n, [n].index(n))
deep = None
for i in range(3000):
    deep = Node(deep)
sys.setrecursionlimit(150)
try:
    repr(deep)
except RecursionError as e:
    print("RecursionError", e)
try:
    deep == Node(deep.child)
except RecursionError as e:
    print("RecursionError", e)
sys.setrecursionlimit(1000)

# A key function called by sort: its caller frame, and a traceback through the builtin.
callers = set()
def key(x):
    if x == 13:
        raise ValueError("thirteen")
    callers.add(sys._getframe(1).f_code.co_name)
    return -x
def sort_it(xs):
    return sorted(xs, key=key)
print(sort_it(range(5)), callers)
try:
    sort_it(range(20))
except ValueError:
    tb = traceback.extract_tb(sys.exc_info()[2])
    print([(fr.name, fr.line) for fr in tb])

# A profiler switched on mid-run sees the builtins as C calls and the key function as Python.
events = []
def prof(frame, event, arg):
    if event in ("call", "c_call", "return"):
        events.append((event, frame.f_code.co_name if event != "c_call" else arg.__name__))
for i in range(4):
    if i == 2:
        sys.setprofile(prof)
    sorted([3, 1, 2], key=lambda x: -x)
    "ab".upper()
    max(i, 1)
sys.setprofile(None)
print(events)

# Generators and coroutines running builtins inside.
def gen(n):
    for i in range(n):
        yield str(i).zfill(3) + "ab".upper()
print(list(gen(4)), sum(len(s) for s in gen(30)))
async def co(x):
    return sorted(str(x))
c = co(4321)
try:
    c.send(None)
except StopIteration as e:
    print(e.value)
