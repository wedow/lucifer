# The frame compiled code runs in, observed while it runs. Each hot function
# calls a callee it cannot inline (an argument), with ints, floats and
# objects in its locals, and on one late iteration the callee looks at the
# caller's frame: reads its locals, writes one through f_locals (PEP 667),
# keeps the frame object past the return, takes zero-argument super() and
# locals(), or collects while a cycle only the caller's locals hold is live.
# Another thread reads the locals of a frame blocked in a call. A function
# whose loop starts at its first instruction is entered there both by calls
# and by its loop. Each output line must be the interpreter's.
import gc
import sys
import threading

N = int(sys.argv[1]) if len(sys.argv) > 1 else 300


def nothing(*args):
    return 0


def read_caller(*args):
    f = sys._getframe(1)
    return sorted((k, v) for k, v in f.f_locals.items() if not callable(v))


def write_caller(*args):
    sys._getframe(1).f_locals['x'] = 100
    return 0


kept = []


def keep_caller(*args):
    kept.append(sys._getframe(1))
    return 0


def accumulate(fn, n, late):
    x = 1
    s = 0
    t = 0.5
    seen = None
    for i in range(n):
        s += x
        t *= 1.5
        if i == late:
            seen = fn(i, s)
        else:
            nothing(i)
    return s, t, seen


def run_reads():
    for k in range(N):
        r = accumulate(nothing, 6, -1)
    print('plain', r)
    print('read', accumulate(read_caller, 6, 3))


def run_writes():
    for k in range(N):
        accumulate(nothing, 6, -1)
    # The write makes x 100 from the fourth iteration on.
    print('write', accumulate(write_caller, 6, 3))


def run_kept():
    for k in range(N):
        accumulate(nothing, 6, -1)
    print('kept', accumulate(keep_caller, 6, 2))
    f = kept.pop()
    print('kept locals', sorted((k, v) for k, v in f.f_locals.items() if not callable(v)))
    print('kept line', f.f_code.co_name, f.f_lineno > 0)


class Base:
    def name(self):
        return 'base'


class Derived(Base):
    def name(self):
        n = 0
        for i in range(3):
            n += i
        return super().name() + str(n)


def run_super():
    d = Derived()
    for k in range(N):
        r = d.name()
    print('super', r)


def snapshot(fn, n):
    a = n * 2
    b = float(n)
    c = [n]
    fn(a)
    return sorted((k, v) for k, v in locals().items() if not callable(v))


def run_locals():
    for k in range(N):
        r = snapshot(nothing, k)
    print('locals', r)


class Node:
    def __init__(self):
        self.me = self


def hold(fn, n):
    a = Node()
    total = 0
    for i in range(n):
        total += i
        fn(i)
    return a.me is a, total


def collect(*args):
    gc.collect()
    return 0


def run_gc():
    for k in range(N):
        hold(nothing, 3)
    print('gc', hold(collect, 3))


ready = threading.Event()
release = threading.Event()
shared = []


def blocked(wait, n):
    count = 0
    ratio = 0.25
    for i in range(n):
        count += 3
        ratio += 0.5
        if i == n - 1:
            shared.append(sys._getframe())
            ready.set()
            wait()
    return count, ratio


def run_thread():
    for k in range(N):
        blocked(nothing, 4)
    shared.clear()
    ready.clear()
    t = threading.Thread(target=lambda: print('thread', blocked(release.wait, 4)))
    t.start()
    ready.wait()
    f = shared[0]
    print('other', sorted((k, v) for k, v in f.f_locals.items() if not callable(v)))
    release.set()
    t.join()


def loop_first(n, fn):
    # The loop's header is the instruction after RESUME: the call's entry.
    # A long loop enters there with `last` bound.
    while n > 0:
        if n < 3:
            fn(last)
        last = n
        n -= 1
    return last


def run_loop_first():
    for k in range(N):
        loop_first(3, nothing)
    print('loop first', loop_first(5000, nothing))


run_reads()
run_writes()
run_kept()
run_super()
run_locals()
run_gc()
run_thread()
run_loop_first()
