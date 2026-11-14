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
