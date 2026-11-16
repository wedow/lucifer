# Exceptions raised and handled in hot code without leaving it: a class's
# __next__ ending a for loop (with __slots__ and without, and a deque's),
# `raise C` caught by a handler in the same function and in a caller, by a
# tuple of classes and by a bare except; a handler that reads what it
# caught, one that does not match, one inside another handler, one in a
# generator (PEP 479), an exception class with its own __init__, a
# handler whose class global is rebound. Each result must be the
# interpreter's, sys.exception() included.
import sys
from collections import deque

N = int(sys.argv[1]) if len(sys.argv) > 1 else 3000


class Countdown:
    __slots__ = ('n',)

    def __init__(self, n):
        self.n = n

    def __iter__(self):
        return self

    def __next__(self):
        if self.n == 0:
            raise StopIteration
        self.n -= 1
        return self.n


class Plain:
    def __init__(self, n):
        self.n = n

    def __iter__(self):
        return self

    def __next__(self):
        if self.n <= 0:
            raise StopIteration
        self.n -= 2
        return self.n


class Loud(Exception):
    made = 0

    def __init__(self, *args):
        Loud.made += 1
        super().__init__(*args)


def loops(n):
    s = 0
    d = deque([1, 2, 3])
    for i in range(n):
        for x in Countdown(i % 5):
            s += x
        for x in Plain(i % 7):
            s += x
        for x in d:
            s += x
    return s


def same_frame(n):
    hits = 0
    for i in range(n):
        try:
            if i % 3:
                raise KeyError
            hits += 1
        except KeyError:
            hits += 2
    return hits, sys.exception()


def check(i):
    if i % 4 == 0:
        raise IndexError
    if i % 4 == 1:
        raise KeyError
    return i


def in_caller(n):
    s = 0
    for i in range(n):
        try:
            s += check(i)
        except (KeyError, IndexError):
            s -= 1
    return s


def bare(n):
    s = 0
    for i in range(n):
        try:
            check(i)
        except:
            s += 1
    return s


def seen(n):
    names = set()
    for i in range(n):
        try:
            check(i)
        except KeyError as e:
            names.add((type(e).__name__, e.__traceback__.tb_next is not None))
        except IndexError:
            names.add(type(sys.exception()).__name__)
    return sorted(names, key=str)


def inner_miss(i):
    try:
        check(i)
    except KeyError:
        return 'key'
    return 'none'


def not_matched(n):
    depths = set()
    for i in range(n):
        try:
            inner_miss(i)
        except IndexError as e:
            tb, d = e.__traceback__, 0
            while tb is not None:
                d += 1
                tb = tb.tb_next
            depths.add(d)
    return sorted(depths)


def nested(n):
    out = set()
    for i in range(n):
        try:
            raise ValueError(i)
        except ValueError:
            try:
                raise StopIteration
            except StopIteration:
                pass
            out.add(type(sys.exception()).__name__)
    return sorted(out), sys.exception()


def gen(k):
    yield k
    raise StopIteration


def in_generator(n):
    kinds = set()
    for i in range(n):
        try:
            for x in gen(i):
                pass
        except RuntimeError as e:
            kinds.add(type(e.__cause__).__name__)
    return sorted(kinds)


def loud(n):
    Loud.made = 0
    s = 0
    for i in range(n):
        try:
            raise Loud
        except Loud:
            s += 1
    return s, Loud.made


Caught = KeyError


def rebound(n):
    global Caught
    s = 0
    for i in range(n):
        if i == n // 2:
            Caught = IndexError
        try:
            try:
                raise KeyError
            except Caught:
                s += 1
        except KeyError:
            s += 100
    Caught = KeyError
    return s


print(loops(N))
print(same_frame(N))
print(in_caller(N))
print(bare(N))
print(seen(N))
print(not_matched(N))
print(nested(N))
print(in_generator(N))
print(loud(N))
print(rebound(N))
