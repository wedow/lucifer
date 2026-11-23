# Exceptions raised in a hot loop of user code and taken by a handler of
# the same function: a dict miss, a list index, an attribute lookup
# through getattr, next() on an exhausted iterator, raise of a class and
# of an instance, raise ... from, and an exception from a call that is
# not made in line. The handlers drop the exception, read it (as e, its
# args, its traceback's lines, the frame's locals through the traceback,
# sys.exception()), chain another to it, raise it again bare inside an
# inner try, and run finally and with blocks. Each output line must be
# the interpreter's.
import sys
import traceback

N = int(sys.argv[1]) if len(sys.argv) > 1 else 3000


class Oops(Exception):
    pass


class Box:
    x = 1


class Ctx:
    def __init__(self):
        self.n = 0

    def __enter__(self):
        self.n += 1
        return self

    def __exit__(self, *exc):
        self.n += 10 if exc[0] is not None else 0
        return False


def far(j):
    # Large enough to stay a call of its own.
    t = 0
    for k in range(3):
        t += k * j
        t ^= k
        t += j % 7
    if j & 3 == 0:
        raise Oops(j, t)
    return t


def dropped(n):
    d = {1: 2}
    xs = [1, 2, 3]
    it = iter(())
    b = Box()
    s = 0
    for j in range(n):
        try:
            s += d[j & 1]
        except KeyError:
            s -= 1
        try:
            s += xs[j % 5]
        except IndexError:
            s -= 2
        try:
            s += getattr(b, 'y' if j & 1 else 'x')
        except AttributeError:
            s -= 3
        try:
            s += next(it)
        except StopIteration:
            s += 4
        try:
            if j & 1:
                raise Oops
            s += 1
        except Oops:
            s -= 5
        try:
            s += far(j)
        except Oops:
            s -= 6
    return s


def read(n):
    d = {}
    out = []
    for j in range(n):
        try:
            d[j % 3]
        except KeyError as e:
            if j < 3:
                out.append((type(e).__name__, e.args, [f.lineno for f in traceback.extract_tb(e.__traceback__)],
                            e.__traceback__.tb_frame.f_locals['j'], sys.exception() is e))
        try:
            far(j)
        except Oops as e:
            if j < 8:
                out.append((e.args, [f.name for f in traceback.extract_tb(e.__traceback__)]))
        try:
            raise Oops(j) from ValueError(j)
        except Oops as e:
            if j < 2:
                out.append((e.args, repr(e.__cause__), e.__suppress_context__))
    return out


def chained(n):
    out = []
    for j in range(n):
        try:
            try:
                {}[j]
            except KeyError:
                raise Oops(j)
        except Oops as e:
            if j < 2:
                out.append((e.args, repr(e.__context__)))
        try:
            try:
                [][j]
            except IndexError:
                if j & 1:
                    raise
        except IndexError as e:
            if j < 4:
                out.append(('again', str(e), len(traceback.extract_tb(e.__traceback__))))
    return out


def cleanup(n):
    c = Ctx()
    s = 0
    done = 0
    for j in range(n):
        try:
            try:
                s += {}[j]
            finally:
                done += 1
        except KeyError:
            s += 1
        try:
            with c:
                if j & 1:
                    raise Oops(j)
        except Oops:
            s += 2
    return s, done, c.n


def escapes(n):
    # The handler does not match: the exception leaves the function.
    s = 0
    try:
        for j in range(n):
            try:
                s += {0: 1}[j]
            except IndexError:
                s -= 1
    except KeyError as e:
        return s, e.args, [f.lineno for f in traceback.extract_tb(e.__traceback__)]


print(dropped(N))
print(read(N))
print(chained(N))
print(cleanup(N))
print(escapes(N))
print(sys.exception())
