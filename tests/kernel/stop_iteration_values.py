# StopIteration made every way: called, raised as a class from Python and
# from the runtime, by a generator's return, and by a subclass with its own
# __init__. Its value is the first argument, None without one.


def show(e):
    print(type(e).__name__, e.args, repr(e.value), repr(str(e)))


show(StopIteration())
show(StopIteration(None))
show(StopIteration(5))
show(StopIteration(1, 2))
show(StopIteration((3, 4)))
try:
    StopIteration(value=1)
except TypeError as e:
    print('TypeError', e)


def raise_class():
    raise StopIteration


def returns(v):
    return v
    yield


for f in (raise_class, lambda: next(iter(())), lambda: next(returns(None)), lambda: next(returns(7)),
          lambda: returns((8, 9)).send(None)):
    try:
        f()
    except StopIteration as e:
        show(e)


class Mine(StopIteration):
    def __init__(self, *args):
        super().__init__('mine', *args)
        self.extra = len(args)


try:
    raise Mine
except StopIteration as e:
    show(e)
    print(e.extra)


async def coro(v):
    return v


for v in (None, 0, 'x'):
    try:
        coro(v).send(None)
    except StopIteration as e:
        show(e)
