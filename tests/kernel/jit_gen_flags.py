# What RETURN_GENERATOR makes runs in its own frame whatever its code's
# flags say: code that yields with CO_GENERATOR cleared (a coroutine, by
# _Py_MakeCoro's rule) is resumed, yields and returns in compiled code as
# in the interpreter, and its return finishes it.
CO_GENERATOR = 0x20


def template():
    x = yield 1
    print('in', x)
    return 2


def looping(n):
    total = 0
    for i in range(n):
        total += yield i
    return total


def unflagged(f):
    return type(f)(f.__code__.replace(co_flags=f.__code__.co_flags & ~CO_GENERATOR), globals())


def drive(r):
    seen = []
    try:
        seen.append(r.send(None))
        while True:
            seen.append(r.send(10))
    except StopIteration as stop:
        seen.append(('returned', stop.value))
    try:
        r.send(None)
    except Exception as e:
        seen.append(type(e).__name__)
    r.close()
    return seen


for f, args in ((template, ()), (unflagged(template), ()), (looping, (5,)), (unflagged(looping), (5,))):
    for turn in range(40):
        r = f(*args)
        print(turn, type(r).__name__, drive(r))
        try:
            [].remove(turn)
        except ValueError as e:
            print(type(e).__name__, e.__context__)


# The same code raising, and awaited from a coroutine whose own unit
# resumes it: either way it is finished and its frame gone.
def raising(n):
    for i in range(n):
        yield i
    raise KeyError(n)


async def awaiting(c):
    return await c


coro_raising, coro_looping = unflagged(raising), unflagged(looping)
for turn in range(40):
    r = coro_raising(3)
    try:
        drive(r)
    except KeyError as e:
        print(turn, 'raised', e, r.cr_running, r.cr_frame)
    print(turn, drive(awaiting(coro_looping(3))))


# The reverse: a function whose code has the generator flag but does not
# start with RETURN_GENERATOR is a plain function, its frame a plain one.
def plain(n):
    return [n * 2 for _ in range(2)], n


flagged = type(plain)(plain.__code__.replace(co_flags=plain.__code__.co_flags | CO_GENERATOR), globals())
print([flagged(i) for i in range(40)][-3:], sum(flagged(i)[1] for i in range(40)))
