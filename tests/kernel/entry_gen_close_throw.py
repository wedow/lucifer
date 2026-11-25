# close, throw and send on generators whose turns ran in compiled code: one
# suspended at a yield, one running (from its own body, and from another
# thread), a delegation chain, a finally that yields on GeneratorExit, the
# generator's attributes between turns, and one dropped unfinished.
import sys
import threading

log = []


def counter(n):
    try:
        for i in range(n):
            got = yield i
            if got is not None:
                log.append(('sent', got))
    except KeyError as e:
        log.append(('caught', e.args[0]))
        yield 'after throw'
    finally:
        log.append('finally')


def turns(g, k):
    s = 0
    for _ in range(k):
        s += next(g)
    return s


for _ in range(100):
    turns(counter(50), 40)

g = counter(1000)
print(turns(g, 300), g.send('x'), g.throw(KeyError('k')), log)
log.clear()
g = counter(1000)
turns(g, 300)
g.close()
print('closed', log, g.gi_frame)
log.clear()


# Running: from its own body, and from another thread.
def selfish():
    me = yield
    for op in ('send', 'throw', 'close'):
        try:
            if op == 'send':
                me.send(1)
            elif op == 'throw':
                me.throw(KeyError)
            else:
                me.close()
        except ValueError as e:
            log.append((op, str(e)))
        yield op


g = selfish()
next(g)
print([g.send(g)] + [next(g) for _ in range(2)], log)
log.clear()

inside = threading.Event()
release = threading.Event()


def slow():
    for i in range(10 ** 9):
        if i == 300:
            inside.set()
            release.wait()
            return
        yield i


g = slow()
t = threading.Thread(target=lambda: sum(g))
t.start()
inside.wait()
for op in (g.send, lambda v: g.throw(KeyError(v)), lambda v: g.close()):
    try:
        op(None)
    except ValueError as e:
        log.append(str(e))
print('running', g.gi_running, log)
release.set()
t.join()
log.clear()


# A throw down a delegation chain whose members ran hot.
def leaf():
    try:
        while True:
            yield 1
    except KeyError:
        log.append('leaf caught')
        yield 'leaf'


def middle():
    try:
        r = yield from leaf()
    finally:
        log.append('middle finally')


def top():
    yield from middle()


for _ in range(50):
    turns(top(), 30)
g = top()
turns(g, 200)
print('chain', g.throw(KeyError), g.gi_yieldfrom.__name__, log)
g.close()
print(log)
log.clear()


# GeneratorExit through a finally that yields.
def stubborn():
    try:
        while True:
            yield 1
    finally:
        yield 'in finally'


g = stubborn()
turns(g, 200)
try:
    g.close()
except RuntimeError as e:
    print('RuntimeError', e)


# The generator's frame between compiled turns.
def walker():
    total = 0
    for i in range(10 ** 6):
        total += i
        yield i


g = walker()
turns(g, 500)
f = g.gi_frame
print('frame', g.gi_running, g.gi_yieldfrom, f.f_lineno - walker.__code__.co_firstlineno, f.f_locals['total'],
      f.f_locals['i'])
turns(g, 5)
print('frame', f.f_locals['total'], f.f_locals['i'])


# Dropped unfinished.
for _ in range(3):
    g = counter(1000)
    turns(g, 300)
    del g
    print('dropped', log)
    log.clear()
