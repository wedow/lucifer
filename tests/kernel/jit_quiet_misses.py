# Misses CPython answers without making an exception, in hot code: hasattr
# and getattr with a default on instances, classes with __getattr__, types
# and modules, and from-imports of a module's attributes; generators run to
# their end by for, list(), tuple(), join, sum, next() with a default, zip
# and yield from. Lookups that do raise (a property, a __getattr__ or a
# module's __getattr__ raising AttributeError) run a fixed number of times.
# Each result must be the interpreter's; tools/jitcheck also runs this at two
# sizes and requires that the error exits compiled code takes do not grow
# with the iterations.
import sys
import types

N = int(sys.argv[1]) if len(sys.argv) > 1 else 3000


class Plain:
    cls_attr = 'c'

    def __init__(self):
        self.a = 1


class Hooked:
    def __getattr__(self, name):
        if name == 'dyn':
            return 2
        raise AttributeError(name)


class Prop:
    @property
    def p(self):
        raise AttributeError('p')

    @property
    def q(self):
        return 'q'


class Meta(type):
    pass


class WithMeta(metaclass=Meta):
    x = 5


hooked_module = types.ModuleType('hooked')
hooked_module.__getattr__ = lambda name: 'hooked ' + name if name.startswith('h') else hooked_module.miss(name)


def miss(name):
    raise AttributeError(name)


hooked_module.miss = miss


def lookups(n):
    found = missing = 0
    p, h, w = Plain(), Hooked(), WithMeta()
    for i in range(n):
        found += hasattr(p, 'a') + hasattr(p, 'cls_attr') + hasattr(h, 'dyn') + hasattr(int, 'real')
        missing += (not hasattr(p, 'b')) + (not hasattr(Plain, 'b')) + (not hasattr(sys, 'nope'))
        missing += getattr(p, 'b', 1) + getattr(int, 'nope', 1) + getattr(WithMeta, 'nope', 1)
        missing += getattr(w, 'nope', 1) + getattr(types, 'nope', 1) + getattr(Meta, 'nope', 1)
        found += getattr(Prop(), 'q', None) == 'q'
        found += getattr(hooked_module, 'hi', None) == 'hooked hi'
    return found, missing


def raising_lookups():
    # Each of these raises AttributeError inside the lookup, as on CPython.
    seen = []
    for i in range(60):
        seen.append((hasattr(Hooked(), 'nope'), getattr(Prop(), 'p', 'dflt'),
                     getattr(hooked_module, 'zz', 'dflt'), hasattr(hooked_module, 'zz')))
    return seen[0], seen[-1], len(seen)


def from_imports(n):
    t = 0
    for i in range(n):
        from os import path, sep
        from sys import maxsize
        t += len(sep) + (maxsize > 0) + (path is not None)
    return t


def gen(k):
    for i in range(k):
        yield i


def gen_returning(k):
    yield from range(k)
    return 'done'


def delegating(k):
    r = yield from gen_returning(k)
    yield r


def generators(n):
    t = 0
    tail = []
    for i in range(n):
        for x in gen(3):
            t += x
        for x in gen_returning(2):
            t += x
        t += len(list(gen(2))) + len(tuple(gen_returning(3))) + sum(gen(4))
        t += len(''.join(str(x) for x in gen(2)))
        g = gen(1)
        t += next(g, 10) + next(g, 20) + next(g, 30)
        t += sum(a * b for a, b in zip(gen(3), gen(4)))
        tail = list(delegating(i % 3))
    return t, tail


def exhausted_and_running():
    g = gen(1)
    out = [next(g), next(g, 'end'), list(g)]
    try:
        next(g)
    except StopIteration as e:
        out.append(('stop', e.value))

    def selfish():
        yield next(me, 'inner')

    me = selfish()
    try:
        list(me)
    except ValueError as e:
        out.append(str(e))
    r = gen_returning(1)
    out.append(next(r))
    try:
        next(r)
    except StopIteration as e:
        out.append(('returned', e.value))
    return out


def caller_seen_by_generator():
    def g():
        yield sys._getframe(1).f_code.co_name
        yield sys._getframe(0).f_back.f_code.co_name

    names = []
    for i in range(50):
        for name in g():
            names.append(name)
    return names[:2], len(names)


def raises_midway(n):
    def g(k):
        yield 1
        if k:
            raise KeyError(k)
        yield 2

    tbs = []
    for i in range(n):
        try:
            for x in g(i % 2):
                pass
        except KeyError as e:
            tb = e.__traceback__
            names = []
            while tb is not None:
                names.append((tb.tb_frame.f_code.co_name, tb.tb_lineno - FIRST))
                tb = tb.tb_next
            tbs.append(tuple(names))
    return tbs[:1], len(tbs)


FIRST = raises_midway.__code__.co_firstlineno

print(lookups(N))
print(raising_lookups())
print(from_imports(N))
print(generators(N))
print(exhausted_and_running())
print(caller_seen_by_generator())
print(raises_midway(60))
