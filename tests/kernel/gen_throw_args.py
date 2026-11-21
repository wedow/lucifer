import warnings


def gen():
    yield 1


async def coro():
    pass


async def agen():
    yield 1


async def catches():
    try:
        yield 1
    except ValueError:
        yield 2


def show(label, method, *args, **kwargs):
    try:
        method(*args, **kwargs)
    except BaseException as exc:
        print(label, type(exc).__name__, exc)


def show_warning(message, category, filename, lineno, *args, **kwargs):
    print('warning', category.__name__, message,
          filename == show.__code__.co_filename,
          lineno == show.__code__.co_firstlineno + 2)


with warnings.catch_warnings():
    warnings.simplefilter('always', DeprecationWarning)
    warnings.showwarning = show_warning

    g = gen()
    next(g)
    show('generator zero', g.throw)
    show('generator excess', g.throw, ValueError, None, None, None)
    show('generator keyword', g.throw, typ=ValueError)
    show('generator legacy', g.throw, ValueError, ValueError('value'), None)

    c = coro()
    show('coroutine zero', c.throw)
    show('coroutine legacy', c.throw, ValueError, 'two arguments')
    show('coroutine reuse', c.throw, ValueError, None)

    c = coro()
    wrapper = c.__await__()
    show('wrapper excess', wrapper.throw, ValueError, None, None, None)
    show('wrapper legacy', wrapper.throw, ValueError, None)
    c.close()

    awaitables = (('aclose', ()), ('athrow', (ValueError,)), ('asend', (None,)))
    for method, args in awaitables:
        for invalid in ((), (ValueError, None, None, None)):
            a = agen()
            waiter = getattr(a, method)(*args)
            show(method + ' invalid throw', waiter.throw, *invalid)
            print('running after invalid throw', a.ag_running)
            show(method + ' reuse', waiter.throw, *invalid)
            show('generator still usable', a.__anext__().send, None)
            show('cleanup', a.aclose().send, None)

    waiter = agen().aclose()
    show('drive aclose', waiter.send, None)
    show('closed aclose zero', waiter.throw)
    show('closed aclose excess', waiter.throw, ValueError, None, None, None)
    show('closed aclose keyword', waiter.throw, typ=ValueError)

    for method, args in awaitables:
        a = agen()
        waiter = getattr(a, method)(*args)
        show(method + ' legacy throw', waiter.throw, ValueError, 'async')

    a = catches()
    show('first yield', a.__anext__().send, None)
    waiter = a.athrow(KeyError)
    show('caught throw yields', waiter.throw, ValueError)
    show('reuse after yield', waiter.throw)
    show('cleanup', a.aclose().send, None)

    a = agen()
    with warnings.catch_warnings(record=True) as recorded:
        waiter = a.athrow(ValueError, 'direct athrow')
        print('athrow creation warnings', [str(w.message) for w in recorded])
        recorded.clear()
        show('drive athrow', waiter.send, None)
        print('athrow send warnings', len(recorded))

    warnings.simplefilter('error', DeprecationWarning)
    g = gen()
    next(g)
    show('warning as error', g.throw, ValueError, None)
    print('generator still suspended', g.gi_suspended)
    show('single argument', g.throw, ValueError('single'))
