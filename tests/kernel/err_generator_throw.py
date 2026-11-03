def inner():
    yield 1


def outer():
    yield from inner()


g = outer()
next(g)
g.throw(KeyError('thrown into the chain'))
