# throw() whose exception constructor resumes the generators it is thrown into.


class ReInit(Exception):
    def __init__(self, *a):
        try:
            next(g)
        except BaseException as e:
            print('inner', type(e).__name__, e)
        super().__init__(*a)


def sub():
    yield 1


def f():
    try:
        yield 3
    finally:
        yield from sub()


g = f()
for i in range(2):
    try:
        print(g.throw(ReInit))
    except BaseException as e:
        print(type(e).__name__, e)

# The constructor finishes the delegate: the exception goes to its delegator.
s = sub()


class Finish(Exception):
    def __init__(self, *a):
        for x in s:
            print('drained', x)
        super().__init__(*a)


def outer():
    try:
        yield from s
    except Finish:
        print('outer caught Finish')
        yield 'after'


o = outer()
print(next(o))
print(o.throw(Finish))
print(list(o))
