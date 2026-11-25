# What the native attribute paths must leave to Python, seen through hot
# load, store and delete sites (long enough to specialize and to compile):
# a member or getset descriptor copied into a class whose instances it does
# not describe (descr_check raises; reading or writing at its offset would
# touch another object's memory); instance dict keys that are str
# subclasses, which only Python compares with the name, in a managed dict
# and in a dict at tp_dictoffset (ast nodes); a data descriptor whose
# Python __get__ raises; deletes of __slots__ members.
import ast
import types

N = 200


def attempt(f, *args):
    try:
        return f(*args)
    except (AttributeError, TypeError, ValueError, KeyError) as e:
        return type(e).__name__ + ': ' + str(e)


def hot(f, *args):
    """f(*args) N times; the distinct answers, in order of first sight."""
    seen = []
    for _ in range(N):
        r = attempt(f, *args)
        if r not in seen:
            seen.append(r)
    return seen


def load_a(o):
    return o.a


def store_a(o):
    o.a = 1
    return 'stored'


def delete_a(o):
    del o.a
    return 'deleted'


def load_x(o):
    return o.x


def call_x(o):
    return o.x()


def store_x(o, v):
    o.x = v
    return len(o.__dict__)


def delete_x(o):
    del o.x
    return len(o.__dict__)


# A member and getsets of one class, copied into another.
class Slotted:
    __slots__ = ('a',)


class Stranger:
    a = Slotted.a


class StrangerSub(Slotted):
    pass


print('foreign member', hot(store_a, Stranger()), hot(load_a, Stranger()), hot(delete_a, Stranger()))
s = StrangerSub()
print('own member', hot(store_a, s), hot(load_a, s), hot(delete_a, s), hot(load_a, s))
for owner, name in ((types.FunctionType, '__name__'), (types.FunctionType, '__code__'),
                    (BaseException, 'args'), (type, '__name__')):
    d = owner.__dict__[name]
    Holder = type('Holder', (), {'a': d})
    print('foreign', owner.__name__, name, hot(load_a, Holder()), hot(store_a, Holder()), hot(delete_a, Holder()))

# __slots__ deletes, natively: set, deleted, missing, set again.
o = StrangerSub()
o.a = 'v'
print('slot delete', hot(delete_a, o), hot(load_a, o))
o.a = 'again'
print('slot delete again', hot(load_a, o), delete_a(o), hot(load_a, o))


# Instance dict keys that are str subclasses.
class S(str):
    pass


class Plain:
    def x(self):
        return 'method'


class Node(ast.AST):
    def x(self):
        return 'method'


for cls, make in ((Plain, Plain), (Node, Node), (ast.Name, lambda: ast.Name('n'))):
    o = make()
    print(cls.__name__, 'warm', hot(call_x, make()))
    o.__dict__[S('x')] = lambda: 'inst'
    print(cls.__name__, 'load', hot(lambda o: load_x(o)(), o), 'call', hot(call_x, o))
    print(cls.__name__, 'store', hot(store_x, o, lambda: 'new'), hot(call_x, o), sorted(map(type, o.__dict__), key=str))
    print(cls.__name__, 'delete', delete_x(o), hot(call_x, o), hot(delete_x, o))
    o.__dict__[S('y')] = 1
    print(cls.__name__, 'other key', hot(store_x, o, lambda: 'plain'), hot(call_x, o), hot(delete_x, o))


# A str subclass key whose __eq__ answers: it decides whether the name is there.
class Liar(str):
    __hash__ = str.__hash__

    def __eq__(self, other):
        return True


p = Plain()
p.__dict__[Liar('x')] = lambda: 'liar'
print('liar key', hot(call_x, p), hot(store_x, p, lambda: 'stored'), hot(call_x, p), hot(delete_x, p), hot(call_x, p))


# A data descriptor whose Python __get__ raises, with and without __getattr__.
class Raising:
    def __init__(self, exc):
        self.exc = exc

    def __get__(self, obj, owner=None):
        if obj is None:
            return self
        raise self.exc('from __get__')

    def __set__(self, obj, value):
        raise self.exc('from __set__')


class Owner:
    a = Raising(AttributeError)
    x = Raising(ValueError)


class Fallback(Owner):
    def __getattr__(self, name):
        return 'fallback ' + name


for cls in (Owner, Fallback):
    o = cls()
    print(cls.__name__, 'raising get', hot(load_a, o), hot(load_x, o), hot(store_a, o))
Owner.a = Raising(KeyError)
print('raising get swapped', hot(load_a, Owner()), hot(load_a, Fallback()))
