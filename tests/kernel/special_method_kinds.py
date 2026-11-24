# Special methods the kernel's operations call with no, one and two
# arguments after the object, of every kind a class can hold: a function, a
# builtin's slot wrapper, a staticmethod, a classmethod, a callable object
# with and without __get__, a partial, a builtin function; and __init__ of
# each kind called with positional and keyword arguments. Each kind is
# swapped in mid-loop.
import functools


class Callable:
    def __call__(self, *args):
        return ('callable', len(args))


class Binding:
    def __get__(self, obj, owner):
        return lambda *args: ('bound', type(obj).__name__, len(args))


def plain(self, *args):
    return ('function', len(args))


kinds = {
    'function': plain,
    'staticmethod': staticmethod(lambda *args: ('static', len(args))),
    'classmethod': classmethod(lambda cls, *args: ('class', cls.__name__, len(args))),
    'callable': Callable(),
    'binding': Binding(),
    'partial': functools.partial(lambda tag, *args: (tag, len(args)), 'partial'),
}


class Box(dict):
    pass


def attempt(f):
    try:
        return f()
    except Exception as e:
        return type(e).__name__ + ': ' + str(e)


def setitem(b):
    b[1] = 2
    return dict(b)


ops = {
    '__len__': lambda b: attempt(lambda: len(b)),
    '__iter__': lambda b: attempt(lambda: type(iter(b)).__name__),
    '__neg__': lambda b: attempt(lambda: -b),
    '__contains__': lambda b: attempt(lambda: 1 in b),
    '__getitem__': lambda b: attempt(lambda: b[1]),
    '__setitem__': lambda b: attempt(lambda: setitem(b)),
    '__eq__': lambda b: attempt(lambda: b == 3),
    '__format__': lambda b: attempt(lambda: format(b, 'spec')),
    '__pow__': lambda b: attempt(lambda: pow(b, 2, 5)),
}
for name, op in ops.items():
    for i in range(14):
        label = list(kinds)[i % 7] if i % 7 < 6 else 'none'
        if label == 'none':
            if name in Box.__dict__:
                delattr(Box, name)
        else:
            setattr(Box, name, kinds[label])
        r = op(Box())
        if i < 7:
            print(name, label, r)
    if name in Box.__dict__:
        delattr(Box, name)


class Init:
    pass


def init_function(self, *args, **kwargs):
    print('  init function', args, sorted(kwargs))


class InitCallable:
    def __call__(self, *args, **kwargs):
        print('  init callable', len(args), sorted(kwargs))


inits = {
    'function': init_function,
    'staticmethod': staticmethod(lambda *args, **kwargs: print('  init static', len(args), sorted(kwargs))),
    'callable': InitCallable(),
    'wrapper': object.__init__,
    'returns': lambda self, *args, **kwargs: 5,
}
for i in range(10):
    label = list(inits)[i % 5]
    Init.__init__ = inits[label]
    print(label, attempt(lambda: type(Init(1, k=2)).__name__), attempt(lambda: type(Init()).__name__))


class D(dict):
    pass


print(D(a=1), D([(1, 2)], b=3))


# More kinds, each swapped in mid-loop: a method descriptor, a builtin
# function, a __get__ that raises, a bound method stored on the class.
class Raising:
    def __get__(self, obj, owner):
        raise ValueError('no binding')


class Holder:
    def method(self, *args):
        return ('bound method', type(self).__name__, len(args))


more = {
    'method descriptor': dict.get,
    'builtin': abs,
    'raising get': Raising(),
    'bound method': Holder().method,
}
# dict.get only where it gets its key: called with none, its arity error is
# worded differently here.
for name in ('__getitem__', '__len__', '__neg__', '__contains__'):
    labels = list(more) if name in ('__getitem__', '__contains__') else list(more)[1:]
    for i in range(2 * len(labels) + 2):
        label = labels[i % (len(labels) + 1)] if i % (len(labels) + 1) < len(labels) else 'none'
        if label == 'none':
            if name in Box.__dict__:
                delattr(Box, name)
        else:
            setattr(Box, name, more[label])
        r = ops[name](Box({1: 'one'}))
        if i <= len(labels):
            print(name, label, r)
    if name in Box.__dict__:
        delattr(Box, name)

# __init__ as a classmethod and a partial, __new__ returning another class's
# instance (its __init__ not called), and a metaclass __call__.
more_inits = {
    'classmethod': classmethod(lambda cls, *args, **kwargs: print('  init class', cls.__name__, len(args), sorted(kwargs))),
    'partial': functools.partial(lambda tag, *args, **kwargs: print('  init partial', tag, [type(a).__name__ for a in args], sorted(kwargs)), 'first'),
}
for i in range(4):
    label = list(more_inits)[i % 2]
    Init.__init__ = more_inits[label]
    print(label, attempt(lambda: type(Init(1, k=2)).__name__), attempt(lambda: type(Init()).__name__))


class Other:
    def __init__(self, *args):
        print('  Other init', args)


class NewOther:
    def __new__(cls, *args):
        return Other()

    def __init__(self, *args):
        print('  NewOther init', args)


class Meta(type):
    def __call__(cls, *args, **kwargs):
        print('  Meta call', cls.__name__, args, sorted(kwargs))
        return super().__call__(*args)


class Metered(metaclass=Meta):
    def __init__(self, *args):
        print('  Metered init', args)


for i in range(3):
    print(type(NewOther(i)).__name__, type(Metered(i, k=i)).__name__)
