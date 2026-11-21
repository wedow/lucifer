# A mapping function can end an iteration and leave later inputs available.
class End(StopIteration):
    pass


def stop_at_zero(value, offset=0):
    if value == 0:
        raise End("callback ended")
    if value < 0:
        raise ValueError("negative input")
    return value + offset


def in_generator(items):
    for value in items:
        yield value
    yield "loop ended"


def delegate(items):
    return (yield from items)


def show(operation, items):
    try:
        print(operation(items))
    except (StopIteration, RuntimeError, ValueError) as exc:
        print(type(exc).__name__, exc.args)


for make in (lambda values: map(stop_at_zero, values),
             lambda values: map(stop_at_zero, values, (10, 20, 30), strict=True)):
    mapped = make((1, 0, 2))
    show(list, mapped)
    print(next(mapped))

    show(lambda items: list(in_generator(items)), make((1, 0, 2)))
    show(lambda items: next(items, "default"), make((0, 1, 2)))
    show(next, make((0, 1, 2)))
    show(lambda items: items.__next__(), make((0, 1, 2)))
    show(lambda items: next(delegate(items)), make((0, 1, 2)))
    show(list, make((-1, 1, 2)))
