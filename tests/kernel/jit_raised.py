# Exceptions kernel builtins raise without a handler of their own, caught
# by the hot code that called them: StopIteration from send() on a
# generator and a coroutine that finish, KeyError from a dict lookup,
# IndexError from list.pop() and StopIteration from next() on an exhausted
# iterator; and an async generator stepped by hand, whose every value
# reaches its consumer as a StopIteration that the kernel's asend passes on
# through a cleanup handler (a bare raise). What the handler sees (the
# exception, its args, value, context and traceback) must be the
# interpreter's; tools/jitcheck also runs this
# at two sizes and requires that the side exits and the error exits the
# interpreter unwinds do not grow with the iterations: an exception that
# only passes through kernel frames leaves compiled code as the call's
# result, and kernel code raises one again in compiled code.
import sys

N = int(sys.argv[1]) if len(sys.argv) > 1 else 3000


def gen(v):
    return v
    yield


async def coro(v):
    return v


def tb_names(e):
    names = []
    tb = e.__traceback__
    while tb is not None:
        names.append(tb.tb_frame.f_code.co_name)
        tb = tb.tb_next
    return tuple(names)


def caught(n):
    total = 0
    seen = set()
    d = {}
    for i in range(n):
        try:
            gen(i).send(None)
        except StopIteration as e:
            total += e.value
            seen.add(('gen', tb_names(e), e.__context__ is None))
        c = coro(1)
        try:
            c.send(None)
        except StopIteration as e:
            total += e.value
            seen.add(('coro', tb_names(e), e.__context__ is None))
        try:
            d[i]
        except KeyError as e:
            total += e.args[0]
            seen.add(('dict', tb_names(e), e.__context__ is None))
        try:
            [].pop()
        except IndexError as e:
            seen.add(('pop', str(e), tb_names(e)))
        it = iter(())
        try:
            next(it)
        except StopIteration as e:
            seen.add(('next', e.args, tb_names(e)))
    return total, sorted(seen)


def in_handler(n):
    # Raised while another exception is handled: the context is that one.
    seen = set()
    for i in range(n):
        try:
            raise ValueError(i % 2)
        except ValueError:
            try:
                {}[i]
            except KeyError as e:
                seen.add((type(e.__context__).__name__, e.__context__.args, tb_names(e)))
    return sorted(seen)


async def agen(k):
    for i in range(k):
        yield i


def stepped(n):
    total = 0
    for i in range(n // 10):
        g = agen(10)
        while True:
            a = g.__anext__()
            try:
                a.send(None)
            except StopIteration as e:
                total += e.value
            except StopAsyncIteration:
                break
    return total


print(caught(N))
print(stepped(N))
print(in_handler(N))
