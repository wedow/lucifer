# Exceptions raised and handled in hot code: raised in a function and caught
# by its own handler, with and without `as`, by a bare except and by a
# tuple of classes, in nested handlers and finally blocks, in loops that
# continue and break from the handler; a missing dict key caught where it
# is looked up; raised while another is handled (its __context__), from
# another (__cause__), and re-raised. The handlers read the traceback (its
# frames and lines), sys.exception() and sys.exc_info(), which must be the
# interpreter's. Exceptions that leave the functions they are raised in (a
# callee raising to its caller, the runtime's int() and float() of bad
# strings and a bad index, a __next__ ending a for loop, str.translate over
# a mapping) run a fixed number of times. Each result must be the
# interpreter's; tools/jitcheck also runs this at two sizes and requires
# that the error exits compiled code takes do not grow with the iterations,
# nor the exits at a return for the frame object a caught exception's
# traceback made (returns_after_catch is called once per iteration).
import sys

N = int(sys.argv[1]) if len(sys.argv) > 1 else 3000


def lines(tb):
    out = []
    while tb is not None:
        out.append((tb.tb_frame.f_code.co_name, tb.tb_lineno - tb.tb_frame.f_code.co_firstlineno))
        tb = tb.tb_next
    return tuple(out)


def same_frame(n):
    t = 0
    seen = None
    for i in range(n):
        try:
            if i & 1:
                raise KeyError(i)
            t += 1
        except KeyError as e:
            t += e.args[0] & 3
            seen = (lines(e.__traceback__), sys.exception() is e, sys.exc_info()[0].__name__)
        try:
            raise ValueError
        except (TypeError, ValueError):
            t += 2
        try:
            raise IndexError(i)
        except LookupError:
            t += 1
        try:
            raise RuntimeError('x')
        except:
            t += 1
    return t, seen, sys.exception()


def from_operations(n):
    d = {1: 1, 3: 3}
    t = 0
    last = None
    for i in range(n):
        try:
            t += d[i & 3]
        except KeyError as e:
            t -= e.args[0]
            last = lines(e.__traceback__)
    return t, last


def nested_and_finally(n):
    t = 0
    for i in range(n):
        try:
            try:
                raise KeyError(i)
            except KeyError as inner:
                t += 1
                raise ValueError(i) from inner
        except ValueError as outer:
            t += outer.__cause__.args[0] == i
            t += outer.__context__ is outer.__cause__
        finally:
            t += 1
        try:
            try:
                raise TypeError(i)
            finally:
                t += 2
        except TypeError:
            t += 3
        try:
            try:
                raise KeyError(i)
            except KeyError:
                raise OSError(i)
        except OSError as e:
            t += type(e.__context__) is KeyError
    return t


def handler_flow(n):
    t = 0
    for i in range(n):
        try:
            if i % 5 == 0:
                raise StopIteration(i)
        except StopIteration as e:
            if e.value % 10 == 0:
                continue
            t += 1
        t += 2
    j = 0
    while True:
        j += 1
        try:
            if j > n:
                raise LookupError
        except LookupError:
            break
    return t, j


def reraised(n):
    t = 0
    last = None
    for i in range(n):
        try:
            try:
                raise KeyError(i)
            except KeyError:
                t += 1
                raise
        except KeyError as e:
            last = lines(e.__traceback__)
            t += 1
        try:
            try:
                raise ValueError(i)
            except TypeError:
                t -= 1000
        except ValueError:
            t += 1
    return t, last


def callee(i):
    if i & 1:
        raise ZeroDivisionError(i)
    return i


class Countdown:
    def __init__(self, k):
        self.k = k

    def __iter__(self):
        return self

    def __next__(self):
        if self.k == 0:
            raise StopIteration
        self.k -= 1
        return self.k


def leaving():
    # These raise in one function and are caught in another.
    t = 0
    last = None
    items = [1, 2, 3]
    for i in range(80):
        try:
            t += int('12x' if i & 1 else '12')
        except ValueError:
            t += 100
        try:
            t += items[i & 7]
        except IndexError as e:
            t += len(str(e))
        try:
            t += float('nan' if i & 3 else 'oops') > 0
        except ValueError:
            t += 7
        try:
            t += callee(i)
        except ZeroDivisionError as e:
            last = lines(e.__traceback__)
            t -= 1
        for x in Countdown(3):
            t += x
        t += len('a<b>&c'.translate({ord('<'): '&lt;', ord('&'): None}))
    return t, last


def returns_after_catch(i):
    try:
        raise ValueError(i)
    except ValueError as e:
        seen = e.args[0]
    return seen


def calls_catching(n):
    t = 0
    for i in range(n):
        t += returns_after_catch(i)
    return t


print(same_frame(N))
print(calls_catching(N))
print(from_operations(N))
print(nested_and_finally(N))
print(handler_flow(N))
print(reraised(N))
print(leaving())
print(sys.exception())
