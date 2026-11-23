# A name's kind moving under compiled readers on other threads: a loop
# waiting for another thread to replace an int field with a str ends;
# a loop reading a float (an int) field while another thread stores ints
# (floats) there reads only values that were stored, never a number
# made of the other kind's raw word. Fresh classes each round, so each
# round crosses the moment the name generalizes.
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


print('wait', wait_other_thread())
print('float read, int stores', torn(3.5, 7))
print('int read, float stores', torn(7, 3.5))
