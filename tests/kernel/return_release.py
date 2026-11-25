"""A returning frame releases its locals as CPython does: from the last local
to the first, after the frame has left the stack, before its function. The
finalizers that run see the caller on top, may raise (reported to the
unraisable hook), resurrect the frame's function or read a frame object
taken earlier; cells, generators, unwinding and recursion alike."""
import sys
import weakref

log = []
sys.unraisablehook = lambda u: log.append('unraisable ' + type(u.exc_value).__name__ + ' ' + str(u.exc_value))


class D:
    def __init__(self, name, act=None):
        self.name = name
        self.act = act

    def __del__(self):
        log.append('del ' + self.name + ' top=' + sys._getframe(1).f_code.co_name)
        if self.act is not None:
            self.act(self)


def show(title):
    print(title, log)
    log.clear()


def three(x):
    a = D('a')
    b = D('b')
    c = D('c')
    return x


three(1)
show('locals')


def args(p, q, r):
    return 0


args(D('p'), D('q'), D('r'))
show('arguments')


def caller():
    three(1)
    args(D('p'), D('q'), D('r'))


for _ in range(3):
    caller()
show('called from a function')


def raising(_):
    raise ValueError('in del')


def raises():
    a = D('a', raising)
    b = D('b')
    return 2


print(raises())
show('raise in __del__')


# The frame holds the last reference to its function: a local's finalizer
# brings it back through a weak reference, and it runs again.
def resurrect():
    def target(n):
        holder.clear()
        o = D('o', grab)
        return n

    holder = [target]
    ref = weakref.ref(target)
    del target
    back = []

    def grab(_):
        f = ref()
        back.append(f)
        log.append('grabbed ' + (f.__name__ if f is not None else 'None'))

    r = holder[0](3)
    show('resurrect ' + str(r))
    holder.append(back[0])
    print('again', back[0](4), ref() is not None)
    show('resurrect again')


resurrect()

# A frame object taken while the frame ran keeps its locals after the return.
kept = []


def taken():
    a = D('a')
    b = 5
    kept.append(sys._getframe())
    return b


taken()
show('frame object')
print(sorted(kept[0].f_locals), kept[0].f_locals['b'])
kept.clear()
show('frame object freed')


def peek(_):
    log.append('peek ' + str(sorted(kept[0].f_locals)))


def taken_peek():
    first = D('first', peek)
    kept.append(sys._getframe())
    second = D('second')
    return 1


taken_peek()
show('frame object, finalizers')
kept.clear()
show('frame object, finalizers freed')


# Cells and free variables.
def cells():
    a = D('cell-a')
    b = D('plain-b')

    def inner():
        return a

    c = D('plain-c')
    return inner


inner = cells()
show('cells')
del inner
show('closure freed')


def free_vars():
    x = D('free-x')
    y = D('free-y')

    def use():
        z = D('local-z')
        return x.name + y.name

    return use()


print(free_vars())
show('free variables')


# Exceptions unwinding through frames with locals.
def deep(n):
    a = D('a%d' % n)
    if n == 0:
        raise KeyError('bottom')
    b = D('b%d' % n)
    deep(n - 1)


try:
    deep(2)
except KeyError as e:
    print('caught', e)
show('unwind')


# Generators and coroutines.
def gen():
    a = D('gen-a')
    b = D('gen-b')
    yield 1
    c = D('gen-c')
    return 2


g = gen()
print(next(g))
try:
    next(g)
except StopIteration as e:
    print('stop', e.value)
show('generator')


async def coro():
    a = D('co-a')
    b = D('co-b')
    return 7


try:
    coro().send(None)
except StopIteration as e:
    print('coro', e.value)
show('coroutine')


# Recursion to the limit, and the depth after RecursionError.
def down(n):
    a = D('r') if n % 50 == 0 else None
    return down(n + 1)


sys.setrecursionlimit(300)
depth = [0]


def count(n):
    depth[0] = n
    return count(n + 1)


try:
    count(0)
except RecursionError:
    pass
print('depth', depth[0])
try:
    down(0)
except RecursionError:
    pass
print('recursion dels', len(log))
log.clear()
try:
    count(0)
except RecursionError:
    pass
print('depth again', depth[0])


# The deepest frame's finalizer runs with the depth its frame gave back.
class Quiet:
    __del__ = lambda s: None


def deepest(n):
    q = Quiet()
    return deepest(n + 1)


sys.setrecursionlimit(100)
try:
    deepest(0)
except RecursionError:
    pass
print('deepest finalizers', log)


# A finalizer run at a normal return finds the depth the returning frame gave
# back, from the middle of the stack and at its limit.
def room(n=0):
    try:
        return room(n + 1)
    except RecursionError:
        return n


class Room:
    def __del__(self):
        log.append('del room %d' % room())


def returns():
    r = Room()
    return 1


def at(n):
    if n:
        return at(n - 1)
    log.append('caller room %d' % room())
    return returns()


sys.setrecursionlimit(200)
top = room()
for d in (10, top - 4, top - 3, top - 2, top - 1, top):
    try:
        at(d)
    except RecursionError:
        log.append('RecursionError')
    print('room', top - d, log)
    log.clear()
