# marshal.dumps over the constants a compiler produces, and what loads reads back.
# The values are built at run time: the free-threaded oracle makes constants
# loaded from a pyc immortal, which changes where marshal writes references.
import marshal


def T(*items):
    return tuple(list(items))


def I(text):
    return int(text)


def F(text):
    return float(text)


def B(*values):
    return bytes(list(values))


def show(label, value, *args):
    try:
        data = marshal.dumps(value, *args)
        print(label, data.hex())
    except BaseException as e:
        print(label, type(e).__name__, e)


show('none', T(None, True, False, Ellipsis, StopIteration))
show('ints', T(I('0'), I('1'), I('-1'), I('255'), I('2147483647'), I('-2147483648'), I('2147483648'),
               I('-2147483649'), I('4611686018427387904'), I('1267650600228229401496703205376'),
               I('-1267650600228229401496703205376'), I('12345678901234567890123')))
show('floats', T(F('0.0'), F('-0.0'), F('1.5'), F('1e300'), F('inf'), F('-inf'), F('3.14159')))
show('complex', T(complex(0, 1), complex(1, 2), complex(-1.5, -0.5)))
show('strs', T('', 'a', 'hello', 'café', '€' * 3, 'x' * 300, '\U0001f600', '\ud800'))
show('bytes', T(B(), B(97, 98, 99), bytes(list(range(256)))))
big = I('1000000000000')
show('shared', T(big, big, 'abc', 'abc'))
shared = T(I('1'), I('2'))
show('tuple ref', T(shared, shared, [shared]))
show('big tuple', tuple(list(range(300))))
show('list', [I('1'), 'two', F('3.0'), [I('4')], T()])
show('dict', {'a': I('1'), 2: 'b', T(3, 4): None})
show('sets', T({3, 1, 2}, frozenset(['b', 'a', 'c']), set(), frozenset()))
show('slice', slice(I('1'), I('2'), I('3')))
show('bytearray', bytearray(b'xyz'))
show('memoryview', memoryview(b'xyz'))
show('v0', T(I('1'), F('1.5'), 'a'), 0)
show('v1', T(I('1'), F('1.5'), complex(0, 1), 'a', complex(1, 2)), 1)
show('v2', T('a', 'a', F('1.5')), 2)
show('v3', T('abc', 'abc', F('1.5')), 3)
show('v4', T('abc', F('1.5')), 4)
show('unmarshallable', object())
show('bad type', {1, object()})
show('nested', T(T(T(T(I('1'))))))
deep = ()
for _ in range(3000):
    deep = (deep,)
show('too deep', deep)
cyc = []
cyc.append(cyc)
show('cycle', cyc)


def f(a, b=2, *args, c, **kw):
    return [a + b, c, 'const', 1.5, (1, 2, 3), {'x'} & {'x'}]


code = f.__code__
try:
    marshal.dumps(code, allow_code=False)
except ValueError as e:
    print('code disallowed', e)
loaded = marshal.loads(marshal.dumps(code))
print(loaded.co_name, loaded.co_filename, loaded.co_firstlineno, loaded.co_flags)
print(marshal.dumps(code) == marshal.dumps(loaded))
print(marshal.loads(marshal.dumps(T(1, 'two', [3.0, b'4'], {'five': 5j}, frozenset([6])))))
