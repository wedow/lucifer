import exc_cross as x
import traceback


def tb(e):
    return [(f.name, f.line) for f in traceback.extract_tb(e.__traceback__)]


def boom(*a):
    raise ValueError('boom', *a)


def fine(*a):
    return ('fine',) + a


print(x.call(fine, 1, 2), x.occurred())

# C calls Python that raises; Python catches it
try:
    x.call(boom, 1)
except ValueError as e:
    print('caught', e.args, tb(e), x.occurred())

# a raise from C passes through Python and is caught
for t in (ValueError, KeyError, OSError, StopIteration):
    try:
        x.raise_(t, 'from C')
    except t as e:
        print(t.__name__, repr(e), str(e), tb(e))
try:
    x.raise_fmt(TypeError, 'fmt', 3, 'obj')
except TypeError as e:
    print(e)
try:
    x.raise_fmt(TypeError, 'fmt', -3, 'é€')
except TypeError as e:
    print(e)

# Get/Set round trip keeps the same object and traceback
e0 = []


def keep():
    try:
        boom()
    except ValueError as e:
        e0.append(e)
        raise


try:
    x.roundtrip(keep)
except ValueError as e:
    print('roundtrip', e is e0[0], tb(e))
print(x.roundtrip(fine))

# C catches a Python raise by type, and lets others through
r = x.catch(boom, ValueError)
print('catch', type(r).__name__, r.args, tb(r))
print(x.catch(fine, ValueError))
try:
    x.catch(boom, KeyError)
except ValueError as e:
    print('passed through', e.args)
print(x.swallow(boom), x.swallow(fine), x.occurred())

# SetObject with an instance; context chaining from a raise inside except
inst = RuntimeError('inst')
try:
    x.raise_instance(inst)
except RuntimeError as e:
    print(e is inst, tb(e))
try:
    try:
        raise KeyError('first')
    except KeyError:
        x.raise_(ValueError, 'second')
except ValueError as e:
    print(repr(e), repr(e.__context__), e.__cause__)

# traceback of an error crossing several C and Python frames lists user frames only
def deep(n):
    if n == 0:
        return x.nest(boom, 3)
    return deep(n - 1)


try:
    deep(2)
except ValueError as e:
    print(tb(e))
    print(''.join(traceback.format_exception(e)).replace(__file__, 'FILE'))

# an exception raised in C is not seen again afterwards
try:
    x.raise_(ValueError, 'once')
except ValueError:
    pass
print(x.occurred())
x.raise_(ValueError, 'uncaught')
