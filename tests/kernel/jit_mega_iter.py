# Hot loops of the kernel (str.join's comprehension, set() of an iterable)
# over a deque's iterator (a Python class) and a filter, after each site
# has met more classes than its arms take: the site is megamorphic, but
# each run of its loop meets one class. tools/jitcheck runs this at two
# sizes and requires that compiled calls grow by no more than each loop's
# set-up makes (str.join, deque.__iter__, the StopIteration that ends a
# deque's iteration), not by one per item (a step not taken in line): the
# loop takes the classes it meets most in line. Each result must be the
# interpreter's.
import sys
from collections import deque

N = int(sys.argv[1]) if len(sys.argv) > 1 else 3000


class Countdown:
    def __init__(self, n):
        self.n = n

    def __iter__(self):
        return self

    def __next__(self):
        if self.n == 0:
            raise StopIteration
        self.n -= 1
        return 'ab'[self.n & 1]


class Other(Countdown):
    def __next__(self):
        if self.n == 0:
            raise StopIteration
        self.n -= 1
        return 'c'


def gen(n):
    for i in range(n):
        yield 'g'


SOURCES = [lambda: {'a': 1, 'b': 2}, lambda: {'a', 'b'}, lambda: gen(3), lambda: map(str, 'ab'),
           lambda: iter('ab'), lambda: reversed(['a', 'b']), lambda: filter(None, ['a', '', 'b']),
           lambda: deque(['x', 'y']), lambda: Other(2), lambda: (x for x in 'ab'), lambda: Countdown(2)]


def warm():
    out = []
    for _ in range(300):
        for make in SOURCES:
            out.append(''.join(make()))
            out.append(len(set(make())))
    return out


def hot(n):
    total = 0
    d = deque('xy' * 100)
    words = ['a', '', 'b', 'c'] * 50
    for i in range(n):
        total += len(''.join(d))
        total += len(set(d))
        total += len(''.join(filter(None, words)))
    return total


w = warm()
print(len(w), w[:12])
print(hot(N))
print(''.join(Other(3)), sorted(set(Countdown(5))), ''.join(deque('xyz')))
