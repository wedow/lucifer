# Calls made from inside inlined frames: a method the tier inlines into its
# caller's loop calls a function it cannot inline (a different one at each
# turn). While that call runs, the inlined frames are real: the callee sees
# them through sys._getframe, traceback.extract_stack and the traceback of
# its exception, and a frame object it keeps stays the running frame's.
# Each output line must be the interpreter's; tools/jitcheck also runs this
# at two sizes and requires that calls into compiled code grow by one per
# iteration, the polymorphic callee's, not two.
import sys
import traceback

N = int(sys.argv[1]) if len(sys.argv) > 1 else 3000


def make(k):
    def fn(x):
        return x + k
    return fn


class Task:
    def __init__(self, fn):
        self.fn = fn

    def run(self, x):
        y = x
        r = self.fn(y)
        return (r, y)


def hot(n):
    tasks = [Task(make(1)), Task(make(2)), Task(make(3))]
    s = 0
    for i in range(n):
        s += tasks[i % 3].run(i)[0]
    return s


def peek(x):
    f = sys._getframe(1)
    return [f.f_code.co_name, f.f_lineno, sorted(f.f_locals), f.f_back.f_code.co_name, x]


def stack(x):
    return [(s.name, s.lineno) for s in traceback.extract_stack()[-4:-1]]


def boom(x):
    raise ValueError(x)


def keep(x):
    return sys._getframe(1)


def warm_then(fn, x, n):
    """Task.run, inlined into the loop with a plain callee, meets fn last."""
    task = Task(make(0))
    r = None
    for i in range(n):
        if i == n - 1:
            task.fn = fn
            i = x
        r = task.run(i)
    return r


def recurse_a(n):
    return recurse_b(n)


def recurse_b(n):
    if n == 0:
        return 0
    return recurse_a(n - 1) + 1


print(hot(N))
print(warm_then(peek, 1, 300))
print(warm_then(stack, 2, 300))
try:
    warm_then(boom, 3, 300)
except ValueError:
    print([(s.name, s.lineno, s.line) for s in traceback.extract_tb(sys.exc_info()[2])])
# The frame keep returned is Task.run's, which ran on to its end.
frame, y = warm_then(keep, 4, 300)
print(frame.f_code.co_name, frame.f_locals['y'], frame.f_locals['r'] is frame, y)


held = []


def hold(x):
    held.append(sys._getframe(1))
    return x


class Acc:
    def __init__(self):
        self.total = 0

    def add(self, fn, x):
        v = fn(x)
        self.total += v
        return self.total


def accumulate(n):
    acc = Acc()
    fn = make(0)
    for i in range(n):
        if i == n - 1:
            fn, i = hold, 1000
        acc.add(fn, i)
    return acc.total


print(accumulate(300), held[0].f_code.co_name, held[0].f_locals['v'], held[0].f_back.f_code.co_name)

# Identity across a kept frame: a float computed in the inlined method is a
# local and a stack entry at once when the callee keeps the frame, and the
# interpreter, running on, must see one object (DESIGN 4.5).
class Scale:
    def __init__(self, fn):
        self.fn = fn

    def run(self, x):
        y = x * 1.5
        z = y
        t = (y, self.fn(z), z)
        return t[0] is t[2] and t[0] is y and type(y) is float


def scaled(n):
    s = Scale(make(0.0))
    ok = True
    for i in range(n):
        if i == n - 1:
            s.fn = keep
        ok = s.run(float(i)) and ok
    return ok


print('identity', scaled(300))

# Recursion through an inlined frame up to the limit: the interpreter's frames
# and the same RecursionError.
for i in range(300):
    recurse_a(5)
try:
    recurse_a(100000)
except RecursionError:
    print('RecursionError')
