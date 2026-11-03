def inner():
    yield 1
    raise ValueError('raised in the inner generator')


def outer():
    yield from inner()


for x in outer():
    print('got', x)
