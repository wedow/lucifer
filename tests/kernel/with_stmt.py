def yn(cond):
    return 'yes' if cond else 'no'


class Trace:
    """Prints what the with statement does to it."""

    def __init__(self, name, suppress=False):
        self.name = name
        self.suppress = suppress

    def __enter__(self):
        print('enter', self.name)
        return self

    def __exit__(self, kind, value, tb):
        if kind is None:
            print('exit', self.name, 'clean', yn(value is None), yn(tb is None))
        else:
            print('exit', self.name, kind.__name__, yn(tb is not None))
        return self.suppress


class Boom(Exception):
    pass


def check(label, cond):
    print(label, 'ok' if cond else 'FAIL')


# a clean block
with Trace('a') as t:
    print('inside', t.name)

# the target may be anything assignable
class Holder:
    pass


h = Holder()
with Trace('b') as h.attr:
    print('stored', h.attr.name)

# no target
with Trace('c'):
    print('body')

# several managers exit in reverse order
with Trace('d1') as x, Trace('d2') as y:
    print('both', x.name, y.name)

# an exception passes through __exit__ when it does not suppress
try:
    with Trace('e'):
        raise Boom('boom')
except Boom as e:
    print('propagated')

# and is swallowed when __exit__ returns true
with Trace('f', True):
    raise Boom('swallowed')
print('after f')


# return, break and continue leave the block through __exit__
def early():
    with Trace('g'):
        return 'returned'


print(early())
for i in ('x', 'y'):
    with Trace('h'):
        if i == 'x':
            continue
        break
print('left loop')


# __exit__ raising replaces the exception and chains it
class Raises:
    def __enter__(self):
        return self

    def __exit__(self, kind, value, tb):
        raise Boom('from exit')


try:
    with Raises():
        raise KeyError('inner')
except Boom as e:
    check('exit raised', e.__context__.__class__ is KeyError)

# __enter__ raising never calls __exit__
class EnterFails:
    def __enter__(self):
        raise Boom('enter')

    def __exit__(self, *args):
        print('not reached')


try:
    with EnterFails():
        print('not reached')
except Boom:
    print('enter failed')


# objects that are not context managers
class OnlyEnter:
    def __enter__(self):
        return self


class OnlyExit:
    def __exit__(self, *args):
        pass


for thing in (5, OnlyEnter(), OnlyExit(), object):
    try:
        with thing:
            print('not reached')
    except (TypeError, AttributeError) as e:
        msg, = e.args
        print(type(e).__name__, msg)

# special methods are looked up on the type, not the instance
t = Trace('i')
t.__enter__ = None
t.__exit__ = None
with t:
    print('type wins')

# the exception __exit__ sees is the one in flight
class Sees:
    def __enter__(self):
        pass

    def __exit__(self, kind, value, tb):
        check('same exception', value.__class__ is kind and tb is value.__traceback__)
        return True


with Sees():
    raise Boom('seen')

# nested with and try/finally
try:
    with Trace('outer'):
        with Trace('inner'):
            try:
                raise Boom('nested')
            finally:
                print('finally inside')
except Boom:
    print('nested propagated')
