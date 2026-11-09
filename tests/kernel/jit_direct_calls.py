# Calls between compiled functions that are not inlined: the callee's
# compiled code is entered without the interpreter (a C-entry marker stands
# for the caller), and everything a frame shows must stay as the interpreter
# shows it. Closures (COPY_FREE_VARS before RESUME), arguments that are cells
# (MAKE_CELL), bound methods, defaults, a callee that leaves its compiled code
# at a changed type, one that raises, frames walked from inside the callee,
# the recursion limit, tracing turned on in the middle of a call, and a
# callee that replaces its own code while it runs, then collects.
import gc
import sys
import weakref

N = 3000


def recursive(n):
    # Recursion is never inlined: every level is a call.
    return 0 if n == 0 else 1 + recursive(n - 1)


def make_adder(k):
    def add(x):
        return x + k
    return add


def make_counter(start):
    def count(n):
        total = start
        for i in range(n):
            total += i
        return total
    return count


def capture(x):
    # x is a cell: MAKE_CELL runs before RESUME.
    def get():
        return x
    x += 1
    return get()


class Shape:
    def __init__(self, v):
        self.v = v

    def area(self, scale=2):
        return self.v * scale


def caller_names():
    f = sys._getframe(1)
    names = []
    while f is not None and len(names) < 3:
        names.append(f.f_code.co_name)
        f = f.f_back
    return names


def where():
    return caller_names()


def fails(x):
    if x > N - 3:
        raise ValueError(x)
    return x


def poly(x):
    return x * 2


def drive(n):
    adders = [make_adder(i) for i in range(3)]
    shapes = [Shape(i) for i in range(4)]
    s = 0
    for i in range(n):
        s += recursive(i % 7)
        s += adders[i % 3](i)
        s += capture(i)
        s += shapes[i & 3].area()
        s += shapes[i & 3].area(3)
    return s


def errors(n):
    caught = 0
    for i in range(n):
        try:
            fails(i)
        except ValueError as e:
            caught += 1
            tb = e.__traceback__
            names = []
            while tb is not None:
                names.append(tb.tb_frame.f_code.co_name)
                tb = tb.tb_next
            print('traceback', names)
    return caught


def changing(n):
    # poly is compiled for ints, then gets floats and strs: its compiled
    # code leaves at the changed type in the middle of the call.
    s = []
    for i in range(n):
        v = i if i < n - 20 else (float(i) if i < n - 10 else str(i))
        s.append(poly(v))
    return s[-25:]


def deep():
    try:
        recursive(100000)
    except RecursionError:
        return 'recursion'


def traced(n):
    events = []

    def tracer(frame, event, arg):
        if event == 'call':
            events.append(frame.f_code.co_name)
        return None

    counter = make_counter(5)
    out = 0
    for i in range(n):
        if i == n - 2:
            sys.settrace(tracer)
        out += counter(3)
    sys.settrace(None)
    return out, events


def plain(n):
    return -n


rebind = [False]


def rebinding(n):
    # Every level but the first is a direct call. The innermost replaces
    # the function's code and collects while its frame and its callers'
    # still run the old code, which must stay alive: the traceback reads
    # it after the collection and after other code took the memory.
    if n == 0:
        if rebind[0]:
            old = weakref.ref(rebinding.__code__)
            rebinding.__code__ = plain.__code__
            gc.collect()
            junk = [compile(f'x{i} = {i}', '<junk>', 'exec') for i in range(200)]
            raise KeyError(len(junk), old() is not None)
        return 0
    return rebinding(n - 1) + 1


def rebinds(n):
    s = 0
    for i in range(n):
        s += rebinding(5)
    rebind[0] = True
    try:
        rebinding(5)
    except KeyError as e:
        tb = e.__traceback__
        names = []
        while tb is not None:
            names.append((tb.tb_frame.f_code.co_name, tb.tb_lineno))
            tb = tb.tb_next
        print('rebound', s, names)
    print(rebinding(5), rebinding.__code__ is plain.__code__)


print(drive(N))
print(errors(N))
print(changing(N))
print(where())
for i in range(N):
    names = where()
print(names)
print(deep())
print(traced(N))
rebinds(N)
