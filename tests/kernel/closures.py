def outer(a):
    def inner():
        return a
    return inner


def pair(a, b):
    def both(sep):
        return print_two(a, sep, b)
    return both


def print_two(a, sep, b):
    print(a, sep, b)


def shared():
    x = 'one'

    def get():
        return x

    def put(v):
        nonlocal x
        x = v
    print(get())
    put('two')
    print(get())
    x = 'three'
    print(get())
    return put, get


def deep():
    v = 'deep'

    def middle():
        def innermost():
            return v
        return innermost
    return middle()()


def late():
    def reader():
        return v
    v = 'late'
    return reader()


def cell_arg(a, b='B'):
    def f():
        return b
    a = 'rebound'
    print(a)
    return f()


def default_and_free(x):
    def f(y='dflt'):
        print(x, y)
    return f


def rebuild():
    x = 'first'
    del x
    x = 'second'

    def f():
        return x
    return f()


def clear_cell():
    z = 'z'

    def f():
        return z
    del z
    z = 'again'
    return f()


counter = 'global-start'


def set_global(v):
    global counter
    counter = v


def read_global():
    return counter


def drop_global():
    global counter
    del counter


print(outer('x')())
f = outer('captured')
print(f())
pair('l', 'r')('+')
shared()
print(deep())
print(late())
print(cell_arg('orig'))
default_and_free('free')()
default_and_free('free')('given')
print(rebuild())
print(clear_cell())
print(read_global())
set_global('changed')
print(read_global())
print(counter)
drop_global()
counter = 'restored'
print(read_global())
temp = 'temp'
del temp
temp = 'again'
print(temp)
