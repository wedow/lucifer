# A function's __code__ replaced, by the innermost of its running calls or
# by another thread, with a code of a smaller frame (fewer locals, a
# shallower stack): the calls still running keep the code they started
# with. Their frames, rebuilt when compiled code gives them back, name it
# (f_code, tracebacks), hold their locals and compute with them. Each
# output line must be the interpreter's.
import gc
import sys
import threading

N = int(sys.argv[1]) if len(sys.argv) > 1 else 3000


def small(n):
    return -n


mode = [0]
seen = []
go = threading.Event()
done = threading.Event()


def look():
    out = []
    f = sys._getframe(1)
    while f is not None and f.f_code.co_name == 'deep':
        out.append((f.f_lineno, f.f_locals['c'], f.f_code.co_nlocals))
        f = f.f_back
    return out


# Each round runs a function of its own: one whose __code__ was ever
# assigned is not taken in line again.
SRC = '''
def deep(n):
    a, b, c = n, n + 1, n + 2
    if n == 0:
        if mode[0]:
            if mode[0] == 2:
                go.set()
                done.wait()
            else:
                deep.__code__ = small.__code__
            gc.collect()
            junk = [list(range(30)) for i in range(200)]
            if mode[0] == 3:
                raise KeyError(len(junk))
            seen.append(look())
        return 0
    return deep(n - 1) + a + b + c
'''


def rebind_when_asked(deep):
    go.wait()
    deep.__code__ = small.__code__
    done.set()


def run(depth, how):
    ns = dict(globals())
    exec(compile(SRC, 'deeps', 'exec'), ns)
    deep = ns['deep']
    mode[0] = 0
    seen.clear()
    go.clear()
    done.clear()
    t = threading.Thread(target=rebind_when_asked, args=(deep,))
    t.start()
    s = 0
    for i in range(N):
        s += deep(depth)
    mode[0] = how
    try:
        got = deep(depth)
    except KeyError as e:
        tb = e.__traceback__
        names = []
        while tb is not None:
            names.append((tb.tb_frame.f_code.co_name, tb.tb_lineno))
            tb = tb.tb_next
        got = ('raised', names)
    if how != 2:
        go.set()
    t.join()
    print(depth, how, s, got, seen, deep.__code__ is small.__code__, deep(depth))


for depth in (5, 3, 1):
    for how in (3, 1, 2):
        run(depth, how)
