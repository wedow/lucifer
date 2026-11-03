import sys


class Plain(Exception):
    pass


class Sub(Plain):
    pass


class Loud(Exception):
    def __init__(self, what):
        Exception.__init__(self, what)
        self.what = what


def check(label, cond):
    print(label, 'ok' if cond else 'FAIL')


def one(e):
    msg, = e.args
    return msg


def say(label, e):
    print(label, type(e).__name__, one(e))


# try, except, else, finally, and what runs when
def flow(kind):
    try:
        print('body', kind)
        if kind == 'raise':
            raise Plain('p')
        if kind == 'return':
            return 'returned'
    except Plain:
        print('handler')
    else:
        print('else')
    finally:
        print('finally')
    return 'fell off'


print(flow('quiet'))
print(flow('raise'))
print(flow('return'))

# class hierarchy, tuple matches, the as-name is cleared after the handler
try:
    raise Sub('s')
except (KeyError, Plain) as e:
    say('tuple', e)
    check('isinstance', isinstance(e, Plain) and isinstance(e, Sub) and isinstance(e, Exception))
try:
    e
except NameError:
    print('e is unbound')
try:
    raise Sub
except Exception as e:
    check('class raised', type(e) is Sub and not e.args)

# subclasses with their own __init__
try:
    raise Loud('l')
except Loud as e:
    check('Loud', e.what == 'l' and one(e) == 'l')

# a handler that does not match lets the exception through
try:
    try:
        raise KeyError('k')
    except ValueError:
        print('wrong handler')
except KeyError as e:
    print('outer')

# bare raise re-raises the active exception
try:
    try:
        raise Plain('again')
    except Plain:
        print('inner')
        raise
except Plain as e:
    say('reraised', e)


def inner():
    raise Plain('deep')


def middle():
    inner()


def outer():
    middle()


# traceback entries: one per frame, the innermost last in the chain
try:
    outer()
except Plain as e:
    tb = e.__traceback__
    names = []
    while tb is not None:
        names = [*names, tb.tb_frame.f_code.co_name]
        tb = tb.tb_next
    print(*names)
    tb = e.__traceback__
    check('lines', tb.tb_lineno == 107 and tb.tb_next.tb_lineno == 102)
    check('lasti', tb.tb_lasti >= 0)
    print(tb.tb_frame.f_code.co_filename)

# errors raised by the runtime are catchable and carry a message
try:
    undefined_name
except NameError as e:
    say('name', e)
try:
    inner(1)
except TypeError as e:
    say('call', e)
try:
    object().missing
except AttributeError as e:
    say('attr', e)

# implicit chaining: __context__
try:
    try:
        raise Plain('first')
    except Plain:
        raise Sub('second')
except Sub as e:
    check('context', one(e.__context__) == 'first' and e.__cause__ is None
          and not e.__suppress_context__)

# explicit chaining: raise from
try:
    try:
        raise Plain('cause')
    except Plain as c:
        raise Sub('effect') from c
except Sub as e:
    check('cause', one(e.__cause__) == 'cause' and e.__context__ is e.__cause__
          and e.__suppress_context__)
try:
    try:
        raise Plain('hidden')
    except Plain:
        raise Sub('shown') from None
except Sub as e:
    check('from None', e.__cause__ is None and one(e.__context__) == 'hidden'
          and e.__suppress_context__)

# an exception raised in a finally keeps the earlier one as its context
try:
    try:
        raise Plain('one')
    finally:
        raise Sub('two')
except Sub as e:
    check('finally context', one(e.__context__) == 'one')

# sys.exception and the stack of handled exceptions
check('none outside', sys.exception() is None)
try:
    raise Plain('now')
except Plain as e:
    check('handled', sys.exception() is e)
    try:
        raise Sub('nested')
    except Sub as f:
        check('nested', sys.exception() is f)
    check('restored', sys.exception() is e)
check('cleared', sys.exception() is None)
try:
    raise Plain('info')
except Plain as e:
    t, v, b = sys.exc_info()
    check('exc_info', t is Plain and v is e and b is e.__traceback__)
t, v, b = sys.exc_info()
check('exc_info none', t is None and v is None and b is None)

# things that cannot be raised or caught
try:
    raise 5
except TypeError as e:
    say('not exc', e)
try:
    raise Plain from 5
except TypeError as e:
    say('bad cause', e)
try:
    try:
        raise Plain
    except 5:
        pass
except TypeError as e:
    say('bad except', e)
try:
    raise
except RuntimeError as e:
    say('bare', e)


# break, continue and return through finally
def loop():
    for x in ('a', 'b', 'c'):
        try:
            if x == 'a':
                continue
            if x == 'b':
                break
        finally:
            print('f', x)
    return 'loop done'


print(loop())


def swallow():
    try:
        raise Plain('lost')
    finally:
        return 'swallowed'


print(swallow())

# with_traceback and add_note
e = Plain('wt')
check('with_traceback', e.with_traceback(None) is e and e.__traceback__ is None)
e.add_note('note one')
e.add_note('note two')
print(*e.__notes__)

# assert
try:
    assert 1 == 2, 'assertion text'
except AssertionError as e:
    say('assert', e)
try:
    assert 1 == 2
except AssertionError as e:
    check('assert bare', not e.args)

# str and repr of exceptions
print(Plain('s').__str__())
check('str empty', Plain().__str__() == '')
print(Plain().__repr__(), Sub().__repr__())
check('StopIteration', StopIteration('v').value == 'v' and StopIteration().value is None)
check('SystemExit', SystemExit('c').code == 'c' and SystemExit().code is None)
n = NameError('x', name='nm')
check('NameError', n.name == 'nm' and NameError('x').name is None)
a = AttributeError('x', name='an', obj=n)
check('AttributeError', a.name == 'an' and a.obj is n)
i = ImportError('m', name='mod', path='pth')
check('ImportError', i.msg == 'm' and i.name == 'mod' and i.path == 'pth')
check('args', one(Plain('x')) == 'x')
e = Plain('x')
e.args = ('y', 'z')
y, z = e.args
check('args assigned', y == 'y' and z == 'z')
e.extra = 'extra'
check('instance dict', e.extra == 'extra' and e.__dict__)

# OSError maps errno to its subclass
o = OSError(2, 'No such file')
check('FileNotFoundError', type(o) is FileNotFoundError and o.errno == 2
      and o.strerror == 'No such file' and o.filename is None)
check('PermissionError', type(OSError(13, 'denied')) is PermissionError)
check('plain OSError', type(OSError(1000, 'x')) is OSError and type(OSError('plain')) is OSError)
check('OSError args', OSError('plain').errno is None and OSError(2, 's', 'f').filename == 'f')
check('os hierarchy', issubclass(FileNotFoundError, OSError)
      and issubclass(BrokenPipeError, ConnectionError))

# the hierarchy
for cls in (ZeroDivisionError, FloatingPointError, OverflowError, KeyError, IndexError,
            UnboundLocalError, ModuleNotFoundError, RecursionError, NotImplementedError,
            UnicodeDecodeError, DeprecationWarning, ResourceWarning, GeneratorExit):
    print(cls.__name__, cls.__base__.__name__)

# __getattr__ after __getattribute__ raises AttributeError (a kernel try/except)


class Fallback:
    def __getattr__(self, name):
        return name


class Strict:
    def __getattribute__(self, name):
        raise AttributeError(name)

    def __getattr__(self, name):
        return 'via getattr'


print(Fallback().anything)
print(Strict().anything)
