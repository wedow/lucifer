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


def seen(j, n):
    # Early iterations run in the interpreter; late ones in the handler
    # compiled code takes: both are recorded.
    return j < 4 or j >= n - 4


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
            if seen(j, n):
                out.append((type(e).__name__, e.args, [f.lineno for f in traceback.extract_tb(e.__traceback__)],
                            e.__traceback__.tb_frame.f_locals['j'], sys.exception() is e, e.__traceback__.tb_lasti,
                            repr(e.__context__)))
        try:
            far(j)
        except Oops as e:
            if seen(j, n):
                tb = e.__traceback__.tb_next      # far's own entry: its frame is gone, its locals stay
                out.append((e.args, [f.name for f in traceback.extract_tb(e.__traceback__)],
                            sorted(tb.tb_frame.f_locals.items()), tb.tb_lineno, tb.tb_frame.f_lineno))
        try:
            raise Oops(j) from ValueError(j)
        except Oops as e:
            if seen(j, n):
                out.append((e.args, repr(e.__cause__), e.__suppress_context__, e.__traceback__.tb_lasti))
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
            if seen(j, n):
                out.append((e.args, repr(e.__context__)))
        try:
            try:
                [][j]
            except IndexError:
                if j & 1:
                    raise
        except IndexError as e:
            if seen(j, n):
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


class Custom(Exception):
    def __init__(self, *args):
        super().__init__('custom', *args)
        self.extra = len(args)


class Bare(Exception):
    __slots__ = ()


class OwnOS(OSError):
    pass


class Slotted:
    __slots__ = ('x',)


def shapes(n):
    out = []
    d = {}
    o = Slotted()
    total = 0
    for j in range(n):
        try:
            [][j] if j & 1 else {}[j]
        except (KeyError, IndexError) as e:
            total += 1
            if seen(j, n):
                out.append(type(e).__name__)
        try:
            try:
                {}[j]
            except 5:
                pass
        except TypeError as e:
            if seen(j, n):
                out.append(str(e))
        try:
            raise 5
        except TypeError as e:
            if seen(j, n):
                out.append(str(e))
        try:
            raise Custom
        except Custom as e:
            if seen(j, n):
                out.append((e.args, e.extra))
        try:
            raise Bare(j)
        except Bare as e:
            total += e.args[0] & 1
        try:
            raise OwnOS(2, 'no such', 'f%d' % (j & 3))
        except OSError as e:
            if seen(j, n):
                out.append((e.errno, e.strerror, e.filename, type(e).__name__))
        # Instructions that fail with a bit, not an object.
        try:
            del d[j]
        except KeyError:
            total += 2
        try:
            o.y = j
        except AttributeError as e:
            if seen(j, n):
                out.append(str(e))
        try:
            d[[j]] = 1
        except TypeError:
            total += 3
    return total, out


def flow(n):
    # break, continue and return from inside a handler compiled code takes.
    s = 0
    for j in range(n):
        try:
            {}[j]
        except KeyError:
            if j % 7 == 0:
                continue
            s += 1
        try:
            [][j]
        except IndexError:
            if j == n - 2:
                break
            s += 2
    def ret(k):
        for j in range(k):
            try:
                {}[j]
            except KeyError:
                if j == k - 1:
                    return j, sys.exception()
        return None
    return s, ret(n), sys.exception()


def outer_handler(n):
    # Called inside an except: the previous exc_info is not None.
    try:
        raise ValueError('outer')
    except ValueError:
        r = []
        for j in range(n):
            try:
                {}[j]
            except KeyError as e:
                if seen(j, n):
                    r.append((repr(e.__context__), repr(sys.exception())))
            if seen(j, n):
                r.append(repr(sys.exception()))
        return r


def keep_tb(n):
    # A traceback read after the function that made it returned.
    kept = None
    for j in range(n):
        try:
            {}[j]
        except KeyError as e:
            kept = e
    return kept


def closure_root(n):
    k = 3

    def inner(m):
        s = 0
        for j in range(m):
            try:
                s += {0: k}[j & 1]
            except KeyError as e:
                s -= k
                if j >= m - 2:
                    s += e.__traceback__.tb_frame.f_locals['k']
        return s
    return inner(n)


class Dies:
    deaths = 0

    def __hash__(self):
        return 1

    def __del__(self):
        Dies.deaths += 1


def released(n):
    # Every value a failing instruction held is released on the way to
    # the handler: the keys die, each with its exception.
    d = {}
    for j in range(n):
        try:
            d[Dies()]
        except KeyError:
            pass
        try:
            getattr(Dies(), 'missing')      # through kernel frames, the object in the exception too
        except AttributeError:
            pass
        try:
            d[j * 1000003]                  # a key boxed for the lookup alone
        except KeyError:
            pass
    return Dies.deaths


def lasti_wide(n):
    # 200 names: LOAD_ATTR of the last needs EXTENDED_ARG.
    names = ['name%d' % i for i in range(200)]
    src = 'def probe(o, n):\n    if n < 0:\n        return ' + ' + '.join('o.' + x for x in names) + '\n'
    src += '    out = set()\n    for j in range(n):\n        try:\n'
    src += '            o.' + names[-1] + '\n        except AttributeError as e:\n'
    src += '            out.add((e.__traceback__.tb_lasti, e.name))\n'
    src += '    return sorted(out)\n'
    ns = {}
    exec(src, ns)

    class O:
        name0 = 0
    return ns['probe'](O(), max(n, 20000))


print(shapes(N))
print(flow(N))
print(outer_handler(N))
print(sys.exception())
k = keep_tb(N)
print(k.args, [f.lineno for f in traceback.extract_tb(k.__traceback__)], k.__traceback__.tb_frame.f_locals['j'])
print(closure_root(N))
print(released(N))
print(lasti_wide(N))
print(dropped(N))
print(read(N))
print(chained(N))
print(cleanup(N))
print(escapes(N))
print(sys.exception())
