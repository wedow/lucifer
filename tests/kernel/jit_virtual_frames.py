# Frames compiled code keeps virtual, seen from outside: functions inlined
# three deep into a loop call something that cannot be inlined, which walks
# the stack. What it sees (the chain, identities, lines, locals, a write
# through f_locals, a traceback kept past the frames, the frames of the
# thread as another thread lists them) must be what the interpreter shows,
# and the frames must go on with what the walker left them.
import sys
import threading

N = int(sys.argv[1]) if len(sys.argv) > 1 else 3000
log = []
kept = []


def walker_plain(tag):
    f = sys._getframe(1)
    names = []
    while f is not None and f.f_code.co_name != '<module>':
        names.append((f.f_code.co_name, f.f_lineno - f.f_code.co_firstlineno))
        f = f.f_back
    log.append((tag, names))


def walker_identity(tag):
    a = sys._getframe(1)
    b = sys._getframe(1)
    c = sys._getframe(2).f_back
    d = sys._getframe(3)
    log.append((tag, a is b, c is d, a.f_back is sys._getframe(2), a.f_back.f_back is d))


def walker_locals(tag):
    f = sys._getframe(1)
    seen = sorted((k, v) for k, v in f.f_locals.items() if isinstance(v, (int, tuple)))
    back = sorted((k, v) for k, v in f.f_back.f_locals.items() if isinstance(v, (int, tuple)))
    log.append((tag, seen, back))


def walker_write(tag):
    f = sys._getframe(1)
    f.f_locals['x'] = 1000
    f.f_back.f_locals['bonus'] = 7


def walker_keep(tag):
    kept.append(sys._getframe(1))


def walker_raise(tag):
    raise ValueError(tag)


def walker_threads(tag):
    frames = sys._current_frames()
    f = frames[threading.get_ident()]
    names = []
    while f is not None and f.f_code.co_name != '<module>':
        names.append(f.f_code.co_name)
        f = f.f_back
    log.append((tag, names[names.index('inner'):]))


WALKERS = [walker_plain, walker_identity, walker_locals, walker_write, walker_keep, walker_threads]


def quiet(tag):
    return None


def inner(x, i, call):
    pair = (x, i)
    call(i)
    return x + pair[1]


def middle(x, i, call):
    bonus = 0
    y = inner(x + 1, i, call)
    return y + bonus


def outer(i, call):
    z = middle(i * 2, i, call)
    return z - i


def hot(n):
    total = 0
    for i in range(n):
        call = quiet
        if i >= n - 40 and i % 5 == 0:
            call = WALKERS[(i // 5) % len(WALKERS)]
        total += outer(i, call)
    return total


def hot_raising(n):
    total = 0
    for i in range(n):
        try:
            total += outer(i, walker_raise if i % 997 == 996 else quiet)
        except ValueError as e:
            tb = e.__traceback__
            names = []
            while tb is not None:
                names.append((tb.tb_frame.f_code.co_name, tb.tb_lineno - tb.tb_frame.f_code.co_firstlineno))
                tb = tb.tb_next
            log.append(('raised', str(e), names))
            kept.append(e.__traceback__)
    return total


print(hot(N))
print(hot_raising(N))
for entry in log:
    print(entry)
for k in kept:
    if hasattr(k, 'tb_frame'):
        print('traceback', k.tb_frame.f_code.co_name, k.tb_next.tb_frame.f_code.co_name,
              sorted(k.tb_next.tb_next.tb_frame.f_locals))
    else:
        print('frame', k.f_code.co_name, sorted((n, v) for n, v in k.f_locals.items() if isinstance(v, (int, tuple))),
              k.f_back is None or k.f_back.f_code.co_name)
