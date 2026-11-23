# A name's kind moving under compiled readers on other threads: a loop
# waiting for another thread to replace an int field with a str ends;
# a loop reading a float (an int) field while another thread stores ints
# (floats) there reads only values that were stored, never a number
# made of the other kind's raw word; two threads storing the first str
# into different instances while a third reads them; a sum over a list
# that a writer extends with instances whose first value is a str raises
# TypeError or is the sum of the ints, never another number; a str read
# from an instance is never followed by an int read from it through
# another reference. Fresh classes each round, so each round crosses the
# moment the name generalizes.
import sys
import threading

ROUNDS = int(sys.argv[1]) if len(sys.argv) > 1 else 20
READS = 20000


def wait_other_thread():
    class H:
        pass
    h = H()
    h.state = 0
    go = threading.Event()

    def writer():
        go.wait()
        h.state = 'done'

    t = threading.Thread(target=writer)
    t.start()
    spins = 0
    while h.state == 0:
        spins += 1
        if spins == 1000:
            go.set()
    t.join()
    return h.state


SRC = '''
def read(o, n, seen):
    bad = 0
    for i in range(n):
        v = o.x + 0
        if v != A and v != B:
            bad += 1
            seen.append(v)
    return bad
'''


def torn(first, second):
    bad = 0
    seen = []
    for r in range(ROUNDS):
        class C:
            pass
        o = C()
        o.x = first
        ns = {'A': first, 'B': second}
        exec(SRC, ns)
        ns['read'](o, READS, seen)     # compiled against the first kind
        go = threading.Event()

        def writer():
            go.wait()
            for k in range(200):
                o.x = second
                o.x = first

        t = threading.Thread(target=writer)
        t.start()
        go.set()
        bad += ns['read'](o, READS, seen)
        t.join()
    return bad, seen[:3]


def first_strs():
    bad = 0
    for r in range(ROUNDS):
        class C:
            pass
        objs = [C() for _ in range(4)]
        for k, o in enumerate(objs):
            o.x = k + 10
        ns = {'objs': objs}
        exec(READ_ALL, ns)
        ns['read'](100)                 # compiled against ints
        go = threading.Event()

        def writer(o):
            go.wait()
            o.x = 'str'

        ts = [threading.Thread(target=writer, args=(objs[k],)) for k in (1, 2)]
        for t in ts:
            t.start()
        go.set()
        bad += ns['read'](READS // 10)
        for t in ts:
            t.join()
    return bad


READ_ALL = '''
def read(n):
    bad = 0
    for i in range(n):
        for k, o in enumerate(objs):
            try:
                v = o.x + 0
            except TypeError:
                continue
            if v != k + 10:
                bad += 1
    return bad
'''


SUM = '''
def total(items):
    s = 0
    for o in items:
        s += o.x
    return s
'''


def sum_while_extended():
    wrong = raised = 0
    for r in range(ROUNDS):
        class C:
            pass
        items = []
        for i in range(50):
            o = C()
            o.x = i
            items.append(o)
        ns = {}
        exec(SUM, ns)
        for _ in range(200):
            ns['total'](items)          # compiled against ints
        go = threading.Event()

        def writer():
            go.wait()
            for _ in range(20):
                y = C()
                y.x = 'str'
                items.append(y)

        t = threading.Thread(target=writer)
        t.start()
        go.set()
        for _ in range(200):
            try:
                if ns['total'](items) != 1225:
                    wrong += 1
            except TypeError:
                raised += 1
        t.join()
        try:
            ns['total'](items)
        except TypeError:
            raised += 1
    return wrong, raised >= ROUNDS


BACKWARDS = '''
def read(a, b, n):
    bad = 0
    for i in range(n):
        t = a.x
        try:
            m = b.x + 1
        except TypeError:
            continue
        if type(t) is str:
            bad += 1                    # a str, then an int from the same instance
    return bad
'''


def backwards():
    bad = 0
    for r in range(ROUNDS):
        class C:
            pass
        a = C()
        a.x = 5
        ns = {}
        exec(BACKWARDS, ns)
        ns['read'](a, a, 2000)
        go = threading.Event()

        def writer():
            go.wait()
            a.x = 'str'

        t = threading.Thread(target=writer)
        t.start()
        go.set()
        bad += ns['read'](a, a, READS)
        t.join()
    return bad


print('wait', wait_other_thread())
print('float read, int stores', torn(3.5, 7))
print('int read, float stores', torn(7, 3.5))
print('first strs', first_strs())
print('sum while extended', sum_while_extended())
print('backwards', backwards())
