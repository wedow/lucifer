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
