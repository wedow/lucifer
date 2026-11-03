import types


def f(a, b=2):
    return a + b


code = f.__code__
c = code.replace(co_name='renamed', co_qualname='mod.renamed', co_filename='other.py', co_firstlineno=77)
print(c.co_name, c.co_qualname, c.co_filename, c.co_firstlineno, c.co_code == code.co_code, c.co_argcount)
print(code.replace() == code, code.replace(co_consts=(None, 5)) == code, code.replace(co_flags=code.co_flags | 0x80).co_flags)
print(code.__replace__(co_name='x').co_name, code.replace(co_varnames=code.co_varnames) == code)
try:
    code.replace(bogus=1)
except TypeError as e:
    print(e)


def gen():
    yield 1


@types.coroutine
def coro():
    yield


print(coro.__code__.co_flags & 0x100 != 0, list(types.coroutine(gen)()) if False else 'ok')
