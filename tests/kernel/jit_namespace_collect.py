# A namespace a long-lived hot caller learned from is freed once the
# program drops it, as CPython frees it: the caller's compiled code holds
# what it learned (a callee, a class, a generator function, several
# namespaces' functions at one site) only while the program does. Each
# case makes a namespace from one shared code object (which stays alive),
# runs its functions hot from callers that stay alive, drops it, collects,
# and checks that its functions, class and marker object are dead; then
# the same callers run a new namespace's functions. Each output line must
# be the interpreter's.
import gc
import sys
import weakref

N = int(sys.argv[1]) if len(sys.argv) > 1 else 3000


class Marker:
    live = 0

    def __init__(self):
        Marker.live += 1

    def __del__(self):
        Marker.live -= 1


SRC = '''
marker = Marker()

def plain(x):
    return x * SCALE

def rec(x, d):
    return x * SCALE if d == 0 else rec(x, d - 1) + 1

class Thing:
    def __init__(self, k):
        self.k = k

    def method(self, x):
        return x + self.k * SCALE

def gen(n):
    for i in range(n):
        yield i * SCALE

def make(k):
    def inner(x):
        return x * k + SCALE
    return inner

class Last:
    def __del__(self):
        RESULTS.append(CALLER(plain, 50))

last = Last()
'''
CODE = compile(SRC, 'mod', 'exec')
RESULTS = []


def call_direct(f, n):
    t = 0
    for i in range(n):
        t += f(i)
    return t


def call_rec(f, n):
    t = 0
    for i in range(n):
        t += f(i, 3)
    return t


def call_method(obj, n):
    t = 0
    for i in range(n):
        t += obj.method(i)
    return t


def call_gen(g, n):
    t = 0
    for _ in range(n // 10):
        for v in g(10):
            t += v
    return t


def call_closure(f, n):
    t = 0
    for i in range(n):
        t += f(i)
    return t


def call_poly(fs, n):
    t = 0
    for i in range(n):
        t += fs[i % len(fs)](i)
    return t


def load(k, code=CODE):
    ns = {'SCALE': k, 'Marker': Marker, 'RESULTS': RESULTS, 'CALLER': call_direct}
    exec(code, ns)
    return ns


def held(ns):
    return [weakref.ref(ns[k]) for k in ('marker', 'plain', 'rec', 'Thing', 'gen', 'last')]


def case(name, drive, count=1):
    spaces = [load(k + 2) for k in range(count)]
    refs = [r for ns in spaces for r in held(ns)]
    before = drive(spaces)
    del spaces
    gc.collect()
    dead = [r() is None for r in refs]
    again = [load(k + 7) for k in range(count)]
    after = drive(again)
    print(name, before, after, all(dead), Marker.live == count)
    del again
    gc.collect()


case('direct', lambda s: call_rec(s[0]['rec'], N))
case('inlined', lambda s: call_direct(s[0]['plain'], N))
case('method', lambda s: call_method(s[0]['Thing'](3), N))
case('generator', lambda s: call_gen(s[0]['gen'], N))
case('poly', lambda s: call_poly([ns['plain'] for ns in s], N), 3)

# A site that met closures of one namespace only, while cold, and turns hot
# with another namespace's after the first is gone: what it learned of the
# first must not outlive it.
def closure_case():
    ns = load(5)
    before = call_closure(ns['make'](2), 50)
    ref = weakref.ref(ns['marker'])
    del ns
    gc.collect()
    junk = [{'j': i} for i in range(2000)]
    ns = load(6)
    after = call_closure(ns['make'](3), N)
    print('closure', before, after, ref() is None, Marker.live == 1, len(junk))


closure_case()
gc.collect()
print('finalizer calls', sorted(RESULTS))
RESULTS.clear()


# A module reloaded again and again (fresh code each time) while callers
# use the current one: nothing of the old ones stays.
def reload_loop(rounds):
    total = 0
    for r in range(rounds):
        ns = load(r, compile(SRC, 'mod', 'exec'))
        total += call_direct(ns['plain'], N // 10) + call_method(ns['Thing'](r), N // 10)
        del ns
        if r % 8 == 7:
            gc.collect()
    gc.collect()
    return total


print('reload', reload_loop(40), Marker.live, len(RESULTS))


# Frozen while the caller learned from it, unfrozen before it is dropped:
# the namespace still goes.
def frozen_case():
    gc.freeze()
    ns = load(8)
    before = call_direct(ns['plain'], N)
    gc.unfreeze()
    ref = weakref.ref(ns['marker'])
    del ns
    gc.collect()
    print('frozen', before, ref() is None)


frozen_case()


# Dropped and collected while the caller that learned from it still runs:
# gone once the caller has returned.
def run_swapping(holder):
    t = 0
    for i in range(N):
        t += holder[0]['plain'](i)
        if i == N // 2:
            holder[0] = load(10)
            gc.collect()
    return t


def running_case():
    holder = [load(9)]
    ref = weakref.ref(holder[0]['marker'])
    t = run_swapping(holder)
    del holder
    gc.collect()
    print('running', t, ref() is None)


running_case()

# A finalizer brings a learned function back to life; the caller goes on
# calling it.
KEEP = []
SRC2 = """
def kept(x):
    return x * SCALE

class Keeper:
    def __del__(self):
        KEEP.append(kept)

keeper = Keeper()
"""


def resurrect_case():
    ns = {'SCALE': 11, 'KEEP': KEEP}
    exec(SRC2, ns)
    before = call_direct(ns['kept'], N)
    del ns
    gc.collect()
    f = KEEP.pop()
    print('resurrected', before, call_direct(f, N), f.__globals__['SCALE'])


resurrect_case()


# What refers to a function a hot caller learned: what the program holds.
def referrers_case():
    ns = load(12)
    call_direct(ns['plain'], N)
    print('referrers', sorted(type(r).__name__ for r in gc.get_referrers(ns['plain'])))


referrers_case()
gc.collect()
print('live at the end', Marker.live)
