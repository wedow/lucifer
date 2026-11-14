# When compiled code lets go of objects: each finalizer and weakref
# callback runs where the interpreter runs it, in its order, whether the
# value sat in a local, on the stack, in a tuple that never existed, or in
# the frame of a call that was inlined; and what a finalizer sees of the
# frames (through sys._getframe) is what the interpreter shows it.
import sys
import weakref

N = int(sys.argv[1]) if len(sys.argv) > 1 else 3000
log = []


class Noisy:
    def __init__(self, name):
        self.name = name

    def __del__(self):
        f = sys._getframe(1)
        log.append(('del', self.name, f.f_code.co_name))


def watch(name):
    o = Noisy(name)
    r = weakref.ref(o, lambda _: log.append(('gone', name)))
    return o, r


def rebind(n):
    for i in range(n):
        late = i >= n - 2
        a = Noisy(('a', i)) if late else None
        log.append(('made', i)) if late else None
        a = None
        log.append(('after', i)) if late else None


def temporaries(n):
    for i in range(n):
        late = i >= n - 2
        if late:
            log.append(('before', i))
            Noisy(('t', i))
            log.append(('between', i))
            x = (Noisy(('u', i)), Noisy(('v', i)))[0].name
            log.append(('after', i, x))


def helper(o, i):
    p = (o, i)
    q = Noisy(('q', i))
    return p[1]


def inlined(n):
    s = 0
    for i in range(n):
        if i >= n - 2:
            o = Noisy(('o', i))
            s += helper(o, i)
            log.append(('returned', i))
            del o
            log.append(('deleted', i))
        else:
            s += i
    return s


def at_return(n):
    first = Noisy('first')
    for i in range(n):
        pass
    log.append('returning')
    return n


def weak(n):
    refs = []
    for i in range(n):
        if i >= n - 3:
            o, r = watch(i)
            refs.append(r)
            log.append(('alive', i, r() is o))
            o = None
            log.append(('dead', i, r() is None))
    return len(refs)


def swapped(n):
    a = Noisy('left')
    b = Noisy('right')
    for i in range(n):
        a, b = b, a
    log.append(('swapped', a.name, b.name))
    del a
    log.append('one left')


rebind(N)
temporaries(N)
print(inlined(N))
print(at_return(N))
print(weak(N))
swapped(N)
log.append('end')
for entry in log:
    print(entry)
