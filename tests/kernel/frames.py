# Frame objects: f_locals write-through (PEP 667), positions, f_back across
# kernel builtins, generator frames, frame.clear() and traceback frames.
import sys


def poke(name, value):
    sys._getframe(1).f_locals[name] = value


def write_through():
    x = 1
    poke('x', 2)
    print('x', x)
    poke('fresh', 3)
    print('fresh in locals', 'fresh' in sys._getframe().f_locals)
    print('fresh value', sys._getframe().f_locals['fresh'])
    y = 5
    del y
    print('y' in sys._getframe().f_locals)


def cells():
    a = 10

    def inner():
        return a
    sys._getframe().f_locals['a'] = 11
    print('cell', inner(), a)
    return inner


def proxy_surface():
    p = 7
    q = 8
    loc = sys._getframe().f_locals
    print(type(loc).__name__, sorted(k for k in loc), len(loc) >= 3)
    print(loc['p'], loc.get('q'), loc.get('zz', 'none'))
    try:
        del loc['p']
    except Exception as e:
        print(type(e).__name__, e)
    try:
        loc['nope']
    except KeyError as e:
        print('KeyError', e)
    loc.update({'p': 70})
    print(p, dict(loc)['q'])
    print(sorted(loc.keys()) == sorted(list(loc)))
    return p


def module_locals():
    f = sys._getframe()
    print(f.f_locals is f.f_globals, f.f_code.co_name)


def positions():
    f = sys._getframe()
    a = f.f_lineno
    b = f.f_lineno
    print(b - a, f.f_lasti > 0, type(f.f_lasti).__name__)
    first = f.f_lasti
    c = sys._getframe().f_lasti
    print(c > first)
    print(f.f_code.co_name, f.f_globals is globals(), f.f_builtins is __builtins__.__dict__
          if hasattr(__builtins__, '__dict__') else True)
    print(f.f_trace, f.f_trace_lines, f.f_trace_opcodes)


def whoami():
    return sys._getframe(1).f_code.co_name


def backs():
    print(whoami())
    f = sys._getframe()
    names = []
    while f is not None:
        names.append(f.f_code.co_name)
        f = f.f_back
    print(names)
    seen = []
    sorted([2, 1], key=lambda v: seen.append(sys._getframe(1).f_code.co_name) or v)
    print(seen)
    print(list(map(lambda v: sys._getframe(1).f_code.co_name, [1])))
    try:
        sys._getframe(1000)
    except ValueError as e:
        print('ValueError', e)
    print(sys._getframe(0) is sys._getframe(0))
    print(sys._getframe().f_back is sys._getframe(1))


def gen():
    x = 1
    yield sys._getframe().f_lineno
    x = 2
    yield x


def generator_frames():
    g = gen()
    fr = g.gi_frame
    print(fr is g.gi_frame, fr.f_back, fr.f_code is gen.__code__, fr.f_lineno)
    print(fr.f_locals)
    print(next(g) - fr.f_lineno + 0 < 100, fr.f_lineno)
    print(fr.f_locals, g.gi_frame is fr, fr.f_back)
    first = fr.f_lasti
    next(g)
    print(fr.f_lasti > first, fr.f_locals)
    for _ in g:
        pass
    print(g.gi_frame)
    g2 = gen()
    next(g2)
    f2 = g2.gi_frame
    del g2
    print(f2.f_locals)

    def inside():
        yield sys._getframe().f_back
    h = inside()
    got = next(h)
    print(got.f_code.co_name)


def clearing():
    def work():
        z = [1]
        return sys._getframe(), z
    f, z = work()
    print(f.f_locals)
    f.clear()
    print(f.f_locals)
    f.clear()
    try:
        sys._getframe().clear()
    except RuntimeError as e:
        print('RuntimeError', e)

    g = gen()
    next(g)
    try:
        g.gi_frame.clear()
    except RuntimeError as e:
        print('RuntimeError', e)
    g = gen()
    print(g.gi_frame.clear(), list(g))
    def runner():
        yield g3.gi_frame
    g3 = runner()
    try:
        next(g3).clear()
    except RuntimeError as e:
        print('RuntimeError', e)


def raiser():
    local = 'kept'
    raise ValueError('boom')


def mid():
    raiser()


def tracebacks():
    try:
        mid()
    except ValueError as e:
        tb = e.__traceback__
    entries = []
    while tb is not None:
        f = tb.tb_frame
        entries.append((f.f_code.co_name, tb.tb_lineno - f.f_code.co_firstlineno,
                        f.f_lineno - f.f_code.co_firstlineno, tb.tb_lasti == f.f_lasti))
        tb = tb.tb_next
    print(entries)
    try:
        mid()
    except ValueError as e:
        tb = e.__traceback__
    deep = tb.tb_next.tb_next
    print(deep.tb_frame.f_locals)
    print(deep.tb_frame.f_back is tb.tb_next.tb_frame)
    print(tb.tb_next.tb_frame.f_back is tb.tb_frame)
    print(tb.tb_frame is sys._getframe())
    print(tb.tb_frame.f_back.f_code.co_name)
    print(tb.tb_frame.f_locals is not None)
    deep.tb_frame.f_locals['local'] = 'changed'
    print(deep.tb_frame.f_locals['local'])

    try:
        [1].index(2)
    except ValueError as e:
        print([t.tb_frame.f_code.co_name for t in iter_tb(e.__traceback__)])


def iter_tb(tb):
    while tb is not None:
        yield tb
        tb = tb.tb_next


def same_frame_twice():
    def f():
        try:
            raise KeyError
        except KeyError as e:
            return e.__traceback__.tb_frame, sys._getframe()
    a, b = f()
    print(a is b)


def keep(n):
    if n == 0:
        return sys._getframe()
    r = keep(n - 1)
    return r


def deep_chain():
    # the innermost frame object holds every caller after they all return
    d = 0
    x = keep(20000)
    while x is not None:
        d += 1
        x = x.f_back
    print(d)


def deep_traceback():
    def rec(n):
        if n == 0:
            raise ValueError('deep')
        rec(n - 1)
    try:
        rec(5000)
    except ValueError as e:
        tb = e.__traceback__
    n = 0
    while tb is not None:
        n += 1
        tb = tb.tb_next
    print(n)


class Body:
    seen = sys._getframe().f_locals
    value = 1


write_through()
inner = cells()
proxy_surface()
module_locals()
positions()
backs()
generator_frames()
clearing()
tracebacks()
same_frame_twice()
deep_chain()
deep_traceback()
print(sorted(k for k in Body.seen if not k.startswith('__')))
print(sys._getframe().f_back, sys._getframe().f_code.co_name)
