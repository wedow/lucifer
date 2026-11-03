# Execution state lives off the C stack: these depths run under a 512 KiB
# stack limit only if Python calls, generator resumption and exception
# unwinding never recurse in C.
import sys

sys.setrecursionlimit(200_000)
print('limit', sys.getrecursionlimit())


def depth(n):
    if n == 0:
        return 0
    return depth(n - 1) + 1


print('recursion', depth(150_000))


def chain(n):
    if n == 0:
        sent = yield 'bottom'
        yield sent
        return 'returned'
    result = yield from chain(n - 1)
    return result


g = chain(10_000)
print('first', next(g))
print('sent', g.send('value'))
try:
    next(g)
except StopIteration as e:
    print('stop', e.value)
print('loop', [x for x in chain(10_000)])


def unwind(n):
    if n == 0:
        raise ValueError('from the bottom')
    unwind(n - 1)


try:
    unwind(100_000)
except ValueError as e:
    entries = 0
    tb = e.__traceback__
    while tb is not None:
        entries += 1
        tb = tb.tb_next
    print('unwound', e, entries)
print('after', depth(1000))
