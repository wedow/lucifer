"""Generators, coroutines and async generators (Objects/genobject.c,
Python/ceval.c's await and async-for helpers).

The layouts, their attributes and resumption are native
(runtime/interp/gen.c). gen_send and gen_throw are frame primitives: they
hand the generator's frame to the running eval loop, so next(), send(),
throw() and close() resume it without recursing in C, and a value comes back
here as an ordinary return. gen_state afterwards tells a yield from a
return. Delegation through `yield from` is a loop over the chain, never a
recursion: a throw resumes the whole chain at once (gen_throw's depth).
"""

from _lucifer import (
    MISSING, error, str_join, static_type, kernel_set, kernel_get, address, alloc, field_get,
    field_set, type_lookup, type_tp_name, is_subtype, is_exception, is_exception_class,
    is_callable, gen_send, gen_throw, gen_state, gen_yieldfrom,
    gen_set_running, gen_clear, gen_flag, gen_set_finalizer, asyncgen_hooks, size,
)
from _lucifer import set_asyncgen_hooks as _set_hooks
from _lucifer import _boot, iter as _iter, object as _object

kernel_type = _boot.kernel_type
call_special = _boot.call_special
lookup_attr = _object.lookup_attr

__all__ = ()

_generator = static_type('generator')
_coroutine = static_type('coroutine')
_async_generator = static_type('async_generator')
_asend = static_type('async_generator_asend')
_athrow = static_type('async_generator_athrow')
_wrapper = static_type('coroutine_wrapper')
_wrapped = static_type('async_generator_wrapped_value')
_traceback = static_type('traceback')

_CREATED, _SUSPENDED_YIELD_FROM, _EXECUTING, _COMPLETED = -3, -1, 0, 1
_CO_ITERABLE_COROUTINE = 0x100
# An awaitable's states (AwaitableState).
_INIT, _ITER, _CLOSED = 0, 1, 2
# gen_flag's flags.
_HOOKS_INITED, _AG_CLOSED, _RUNNING_ASYNC = 0, 1, 2


def _name(x):
    return type_tp_name(type(x))


def _kind(gen):
    t = type(gen)
    return 'coroutine' if t is _coroutine else 'async generator' if t is _async_generator \
        else 'generator'


def _is_gen_or_coro(x):
    return type(x) is _generator or type(x) is _coroutine


def _finished(gen):
    return gen_state(gen) >= _COMPLETED


def _result(gen, value):
    """gen_send_ex's ending: what a resumption returned, or its return
    value raised as StopIteration (StopAsyncIteration for an async
    generator, whose return value is None)."""
    if not _finished(gen):
        return value
    if type(gen) is _async_generator:
        raise StopAsyncIteration
    if value is None:
        raise StopIteration
    raise StopIteration(value)


def _send(gen, value):
    """gen.send(value), or next(gen) when value is MISSING."""
    return _result(gen, gen_send(gen, value))


def _next(gen):
    """The iteration step of a generator, as gen_iternext takes it: the next
    item, or MISSING once it returns, without a StopIteration."""
    if _finished(gen):
        return MISSING
    value = gen_send(gen, MISSING)
    return MISSING if _finished(gen) else value


_iter.next_missing_set(_generator, _next)


def _matches(exc, cls):
    """PyErr_GivenExceptionMatches for a class or an instance."""
    t = exc if is_exception_class(exc) else type(exc)
    return is_exception_class(t) and is_subtype(t, cls)


def _normalized(typ, val, tb):
    """throw_here: the exception that (typ, val, tb) describe."""
    if tb is None:
        tb = MISSING
    elif tb is not MISSING and type(tb) is not _traceback:
        error(TypeError, 'throw() third argument must be a traceback object')
    if is_exception_class(typ):
        if val is MISSING or val is None:
            exc = typ()
        elif is_exception(val) and is_subtype(type(val), typ):
            exc = val
        elif type(val) is tuple:
            exc = typ(*val)
        else:
            exc = typ(val)
    elif is_exception(typ):
        if val is not MISSING and val is not None:
            error(TypeError, 'instance exception may not have a separate value')
        exc = typ
    else:
        error(TypeError, str_join('', (
            'exceptions must be classes or instances deriving from BaseException, not ',
            _name(typ))))
    if tb is not MISSING:
        exc.__traceback__ = tb
    return exc


def _close_iter(yf):
    """gen_close_iter for what is not a generator: its close(), if any."""
    close = lookup_attr(yf, 'close')
    if close is not MISSING:
        close()


def _throw(gen, typ, val, tb, close_on_genexit):
    """_gen_throw, iteratively: find how far down the `yield from` chain the
    exception goes, then resume the chain from there."""
    depth = 0
    g = gen
    while True:
        yf = gen_yieldfrom(g)
        if yf is None:
            break
        if close_on_genexit and _matches(typ, GeneratorExit):
            gen_set_running(gen, depth, True)
            failed = MISSING
            try:
                if _is_gen_or_coro(yf):
                    _close(yf)
                else:
                    _close_iter(yf)
            except BaseException as e:
                failed = e
            gen_set_running(gen, depth, False)
            if failed is not MISSING:
                return _result(gen, gen_throw(gen, failed, depth))
            break
        if _is_gen_or_coro(yf):
            g = yf
            depth += 1
            continue
        throw = lookup_attr(yf, 'throw')
        if throw is MISSING:
            break
        gen_set_running(gen, depth, True)
        try:
            if val is MISSING:
                ret = throw(typ)
            elif tb is MISSING:
                ret = throw(typ, val)
            else:
                ret = throw(typ, val, tb)
        except BaseException as e:
            gen_set_running(gen, depth, False)
            return _result(gen, gen_throw(gen, e, depth))
        gen_set_running(gen, depth, False)
        return ret
    if depth == 0:
        return _result(gen, gen_throw(gen, _normalized(typ, val, tb), 0))
    # The generators above the target run while its exception is made, as
    # in CPython's recursion: a constructor that resumes them fails.
    # A constructor that fails throws its error into the delegator instead.
    gen_set_running(gen, depth - 1, True)
    try:
        exc = _normalized(typ, val, tb)
    except BaseException as e:
        gen_set_running(gen, depth - 1, False)
        return _result(gen, gen_throw(gen, e, depth - 1))
    gen_set_running(gen, depth - 1, False)
    return _result(gen, gen_throw(gen, exc, depth))


def _close_one(gen, exc):
    """gen_close's resumption of one generator whose delegate is closed:
    GeneratorExit (or the delegate's closing error) raised in it."""
    try:
        value = gen_throw(gen, GeneratorExit() if exc is MISSING else exc, 0)
    except GeneratorExit:
        return None
    if not _finished(gen):
        error(RuntimeError, str_join('', (_kind(gen), ' ignored GeneratorExit')))
    return value


def _close(gen):
    """gen_close over the whole `yield from` chain, innermost first, as
    CPython's recursion through gen_close_iter closes it."""
    state = gen_state(gen)
    if state == _CREATED:
        gen_clear(gen)
        return None
    if state >= _COMPLETED:
        return None
    chain = [gen]
    yf = gen_yieldfrom(gen)
    while _is_gen_or_coro(yf) and gen_state(yf) == _SUSPENDED_YIELD_FROM:
        chain.append(yf)
        yf = gen_yieldfrom(yf)
    depth = size(chain) - 1
    failed = MISSING
    if _is_gen_or_coro(yf):
        chain.append(yf)                # suspended at a plain yield
        depth += 1
    elif yf is not None:
        gen_set_running(gen, depth, True)
        try:
            _close_iter(yf)
        except BaseException as e:
            failed = e
        gen_set_running(gen, depth, False)
    result = None
    while depth >= 0:
        if depth > 0:
            gen_set_running(gen, depth - 1, True)
        try:
            result = _close_one(chain[depth], failed) if gen_state(chain[depth]) < _COMPLETED \
                else None
            failed = MISSING
        except BaseException as e:
            failed = e
        if depth > 0:
            gen_set_running(gen, depth - 1, False)
        depth -= 1
    if failed is not MISSING:
        raise failed
    return result


def _throw_method(gen, typ, val, tb):
    return _throw(gen, typ, val, tb, True)


def _repr(gen, what):
    return str_join('', ('<', what, ' object ', gen.__qualname__, ' at ', address(gen), '>'))


@kernel_type(_generator)
class generator:
    def __iter__(self):
        return self

    def __next__(self):
        return _send(self, MISSING)

    def send(self, value, /):
        """send(value) -> send 'value' into generator,
return next yielded value or raise StopIteration."""
        return _send(self, value)

    def throw(self, typ, val=MISSING, tb=MISSING, /):
        """throw(value)
throw(type[,value[,tb]])

Raise exception in generator, return next yielded value or raise
StopIteration.
the (type, val, tb) signature is deprecated,
and may be removed in a future version of Python."""
        return _throw_method(self, typ, val, tb)

    def close(self):
        """close() -> raise GeneratorExit inside generator."""
        return _close(self)

    def __repr__(self):
        return _repr(self, 'generator')


@kernel_type(_coroutine)
class coroutine:
    def send(self, value, /):
        """send(arg) -> send 'arg' into coroutine,
return next iterated value or raise StopIteration."""
        return _send(self, value)

    def throw(self, typ, val=MISSING, tb=MISSING, /):
        """throw(value)
throw(type[,value[,traceback]])

Raise exception in coroutine, return next iterated value or raise
StopIteration.
the (type, val, tb) signature is deprecated,
and may be removed in a future version of Python."""
        return _throw_method(self, typ, val, tb)

    def close(self):
        """close() -> raise GeneratorExit inside coroutine."""
        return _close(self)

    def __await__(self):
        w = alloc(_wrapper, _wrapper)
        field_set(w, 0, self)
        return w

    def __repr__(self):
        return _repr(self, 'coroutine')


@kernel_type(_wrapper)
class coroutine_wrapper:
    """A wrapper object implementing __await__ for coroutines."""

    def __iter__(self):
        return self

    def __next__(self):
        return _send(field_get(self, 0), MISSING)

    def send(self, value, /):
        return _send(field_get(self, 0), value)

    def throw(self, typ, val=MISSING, tb=MISSING, /):
        return _throw_method(field_get(self, 0), typ, val, tb)

    def close(self):
        return _close(field_get(self, 0))


# await, async for (Python/ceval.c, _PyCoro_GetAwaitableIter)

def _awaitable_iter(o):
    t = type(o)
    if t is _coroutine or (t is _generator and o.gi_code.co_flags & _CO_ITERABLE_COROUTINE):
        return o
    f = type_lookup(t, '__await__')
    if f is MISSING:
        error(TypeError, str_join('', ("'", _name(o), "' object can't be awaited")))
    res = call_special(f, o)
    if type(res) is _coroutine or (type(res) is _generator
                                   and res.gi_code.co_flags & _CO_ITERABLE_COROUTINE):
        error(TypeError, '__await__() returned a coroutine')
    if type_lookup(type(res), '__next__') is MISSING:
        error(TypeError, str_join('', (
            "__await__() returned non-iterator of type '", _name(res), "'")))
    return res


def get_awaitable(o, which):
    """GET_AWAITABLE: which is 1 after __aenter__, 2 after __aexit__."""
    if which and type_lookup(type(o), '__await__') is MISSING:
        error(TypeError, str_join('', (
            "'async with' received an object from ", '__aenter__' if which == 1 else '__aexit__',
            ' that does not implement __await__: ', _name(o))))
    it = _awaitable_iter(o)
    if type(it) is _coroutine and gen_yieldfrom(it) is not None:
        error(RuntimeError, 'coroutine is being awaited already')
    return it


def get_aiter(obj):
    """GET_AITER."""
    f = type_lookup(type(obj), '__aiter__')
    if f is MISSING:
        error(TypeError, str_join('', (
            "'async for' requires an object with __aiter__ method, got ", _name(obj))))
    it = call_special(f, obj)
    if type_lookup(type(it), '__anext__') is MISSING:
        error(TypeError, str_join('', (
            "'async for' received an object from __aiter__ that does not implement "
            '__anext__: ', _name(it))))
    return it


def get_anext(aiter):
    """GET_ANEXT."""
    if type(aiter) is _async_generator:
        _init_hooks(aiter)
        return _awaitable(_asend, aiter, None)
    f = type_lookup(type(aiter), '__anext__')
    if f is MISSING:
        error(TypeError, str_join('', (
            "'async for' requires an iterator with __anext__ method, got ", _name(aiter))))
    nxt = call_special(f, aiter)
    try:
        return _awaitable_iter(nxt)
    except BaseException as e:
        raise TypeError(str_join('', (
            "'async for' received an invalid object from __anext__: ", _name(nxt)))) from e


def send(receiver, value):
    """SEND to a receiver that is not a generator: (what it yielded, False),
    or (its return value, True)."""
    if type(receiver) is _asend:
        return _asend_step(receiver, value)
    if value is None and _iter.ends_quietly(type(receiver)):
        item = _iter.next_item(receiver)
        return (None, True) if item is MISSING else (item, False)
    try:
        if value is None and type_lookup(type(receiver), '__next__') is not MISSING:
            return (call_special(type_lookup(type(receiver), '__next__'), receiver), False)
        return (receiver.send(value), False)
    except StopIteration as e:
        return (e.value, True)


def stopiteration_error(exc, kind):
    """PEP 479: a StopIteration (or, in an async generator,
    StopAsyncIteration) leaving the body becomes a RuntimeError."""
    what = ('generator', 'coroutine', 'async generator')[kind]
    if is_subtype(type(exc), StopIteration):
        msg = str_join('', (what, ' raised StopIteration'))
    elif kind == 2 and is_subtype(type(exc), StopAsyncIteration):
        msg = 'async generator raised StopAsyncIteration'
    else:
        return exc
    err = RuntimeError(msg)
    err.__cause__ = exc
    err.__context__ = exc
    return err


# Async generators

def _init_hooks(agen):
    """async_gen_init_hooks: the task's finalizer, then firstiter(agen)."""
    if gen_flag(agen, _HOOKS_INITED):
        return
    gen_flag(agen, _HOOKS_INITED, True)
    firstiter, finalizer = asyncgen_hooks()
    if finalizer is not None:
        gen_set_finalizer(agen, finalizer)
    if firstiter is not None:
        firstiter(agen)


def _awaitable(tp, agen, arg):
    a = alloc(tp, tp)
    field_set(a, 0, agen)
    field_set(a, 1, arg)
    field_set(a, 2, _INIT)
    return a


def _unwrap(agen, step):
    """async_gen_unwrap_value around a resumption: an async yield becomes
    StopIteration of its value; the end of the generator closes it."""
    try:
        r = step()
    except BaseException as e:
        if _matches(e, StopAsyncIteration) or _matches(e, GeneratorExit):
            gen_flag(agen, _AG_CLOSED, True)
        gen_flag(agen, _RUNNING_ASYNC, False)
        raise
    if type(r) is _wrapped:
        gen_flag(agen, _RUNNING_ASYNC, False)
        raise StopIteration(field_get(r, 0))
    return r


@kernel_type(_async_generator)
class async_generator:
    def __aiter__(self):
        return self

    def __anext__(self):
        _init_hooks(self)
        return _awaitable(_asend, self, None)

    def asend(self, value, /):
        """asend(v) -> send 'v' in generator."""
        _init_hooks(self)
        return _awaitable(_asend, self, value)

    def athrow(self, *args):
        """athrow(value)
athrow(type[,value[,tb]])

raise exception in generator.
the (type, val, tb) signature is deprecated,
and may be removed in a future version of Python."""
        _init_hooks(self)
        return _awaitable(_athrow, self, args)

    def aclose(self):
        """aclose() -> raise GeneratorExit inside generator."""
        _init_hooks(self)
        return _awaitable(_athrow, self, None)

    def __repr__(self):
        return _repr(self, 'async_generator')


def _asend_closed():
    error(RuntimeError, 'cannot reuse already awaited __anext__()/asend()')


def _asend_step(self, value):
    """async_gen_asend_send as SEND takes it: (what the generator awaits, False),
    or (the value of an async yield, True), which ends the awaitable."""
    agen, sendval, state = field_get(self, 0), field_get(self, 1), field_get(self, 2)
    if state == _CLOSED:
        _asend_closed()
    if state == _INIT:
        if gen_flag(agen, _RUNNING_ASYNC):
            field_set(self, 2, _CLOSED)
            error(RuntimeError, 'anext(): asynchronous generator is already running')
        if value is None or value is MISSING:
            value = sendval
        field_set(self, 2, _ITER)
    gen_flag(agen, _RUNNING_ASYNC, True)
    try:
        r = _send(agen, value)
    except BaseException as e:
        if _matches(e, StopAsyncIteration) or _matches(e, GeneratorExit):
            gen_flag(agen, _AG_CLOSED, True)
        gen_flag(agen, _RUNNING_ASYNC, False)
        field_set(self, 2, _CLOSED)
        raise
    if type(r) is _wrapped:
        gen_flag(agen, _RUNNING_ASYNC, False)
        field_set(self, 2, _CLOSED)
        return field_get(r, 0), True
    return r, False


def _asend_send(self, value):
    r, done = _asend_step(self, value)
    if done:
        raise StopIteration(r)
    return r


def _asend_throw(self, typ, val, tb):
    agen, state = field_get(self, 0), field_get(self, 2)
    if state == _CLOSED:
        _asend_closed()
    if state == _INIT:
        if gen_flag(agen, _RUNNING_ASYNC):
            field_set(self, 2, _CLOSED)
            error(RuntimeError, 'anext(): asynchronous generator is already running')
        field_set(self, 2, _ITER)
        gen_flag(agen, _RUNNING_ASYNC, True)
    try:
        return _unwrap(agen, lambda: _throw_method(agen, typ, val, tb))
    except BaseException:
        gen_flag(agen, _RUNNING_ASYNC, False)
        field_set(self, 2, _CLOSED)
        raise


def _awaitable_close(self, throw):
    """close() of asend and athrow: GeneratorExit thrown in, which must
    end the generator."""
    if field_get(self, 2) == _CLOSED:
        return None
    try:
        throw(self, GeneratorExit, MISSING, MISSING)
    except BaseException as e:
        if _matches(e, StopIteration) or _matches(e, StopAsyncIteration) \
                or _matches(e, GeneratorExit):
            return None
        raise
    error(RuntimeError, 'coroutine ignored GeneratorExit')


@kernel_type(_asend)
class async_generator_asend:
    def __await__(self):
        return self

    def __iter__(self):
        return self

    def __next__(self):
        return _asend_send(self, MISSING)

    def send(self, value, /):
        """send(value) -> send 'value' into generator,
return next yielded value or raise StopIteration."""
        return _asend_send(self, value)

    def throw(self, typ, val=MISSING, tb=MISSING, /):
        return _asend_throw(self, typ, val, tb)

    def close(self):
        """close() -> raise GeneratorExit inside generator."""
        return _awaitable_close(self, _asend_throw)


def _athrow_running(self, closing):
    field_set(self, 2, _CLOSED)
    error(RuntimeError, 'aclose(): asynchronous generator is already running' if closing
          else 'athrow(): asynchronous generator is already running')


def _athrow_args(args):
    n = size(args)
    if n < 1:
        error(TypeError, 'athrow expected at least 1 argument, got 0')
    if n > 3:
        error(TypeError, str_join('', ('athrow expected at most 3 arguments, got ', str(n))))
    return args[0], args[1] if n > 1 else MISSING, args[2] if n > 2 else MISSING


def _athrow_end(self, agen, closing, e):
    """check_error: the athrow ends; aclose() reports a closed generator as
    StopIteration."""
    gen_flag(agen, _RUNNING_ASYNC, False)
    field_set(self, 2, _CLOSED)
    if closing and (_matches(e, StopAsyncIteration) or _matches(e, GeneratorExit)):
        raise StopIteration
    raise e


def _yield_close(self, agen):
    gen_flag(agen, _RUNNING_ASYNC, False)
    field_set(self, 2, _CLOSED)
    error(RuntimeError, 'async generator ignored GeneratorExit')


def _athrow_send(self, value):
    agen, args, state = field_get(self, 0), field_get(self, 1), field_get(self, 2)
    closing = args is None
    if state == _CLOSED:
        error(RuntimeError, 'cannot reuse already awaited aclose()/athrow()')
    if _finished(agen):
        field_set(self, 2, _CLOSED)
        raise StopIteration
    if state == _INIT:
        if gen_flag(agen, _RUNNING_ASYNC):
            _athrow_running(self, closing)
        if gen_flag(agen, _AG_CLOSED):
            field_set(self, 2, _CLOSED)
            raise StopAsyncIteration
        if value is not None and value is not MISSING:
            error(RuntimeError, "can't send non-None value to a just-started coroutine")
        field_set(self, 2, _ITER)
        gen_flag(agen, _RUNNING_ASYNC, True)
        if closing:
            gen_flag(agen, _AG_CLOSED, True)
            try:
                r = _throw(agen, GeneratorExit, MISSING, MISSING, False)
            except BaseException as e:
                _athrow_end(self, agen, closing, e)
            if type(r) is _wrapped:
                _yield_close(self, agen)
            return r
        typ, val, tb = _athrow_args(args)
        try:
            return _unwrap(agen, lambda: _throw(agen, typ, val, tb, False))
        except BaseException as e:
            _athrow_end(self, agen, closing, e)
    try:
        r = _send(agen, None if value is MISSING else value)
    except BaseException as e:
        if closing:
            _athrow_end(self, agen, closing, e)
        gen_flag(agen, _RUNNING_ASYNC, False)
        raise
    if not closing:
        return _unwrap(agen, lambda: r)
    if type(r) is _wrapped:
        _yield_close(self, agen)
    return r


def _athrow_throw(self, typ, val, tb):
    agen, args, state = field_get(self, 0), field_get(self, 1), field_get(self, 2)
    closing = args is None
    if state == _CLOSED:
        error(RuntimeError, 'cannot reuse already awaited aclose()/athrow()')
    if state == _INIT:
        if gen_flag(agen, _RUNNING_ASYNC):
            _athrow_running(self, closing)
        field_set(self, 2, _ITER)
        gen_flag(agen, _RUNNING_ASYNC, True)
    try:
        r = _throw_method(agen, typ, val, tb)
    except BaseException as e:
        gen_flag(agen, _RUNNING_ASYNC, False)
        field_set(self, 2, _CLOSED)
        if closing and (_matches(e, StopAsyncIteration) or _matches(e, GeneratorExit)):
            raise StopIteration
        raise
    if not closing:
        return _unwrap(agen, lambda: r)
    if type(r) is _wrapped:
        _yield_close(self, agen)
    return r


@kernel_type(_athrow)
class async_generator_athrow:
    def __await__(self):
        return self

    def __iter__(self):
        return self

    def __next__(self):
        return _athrow_send(self, None)

    def send(self, value, /):
        """send(value) -> send 'value' into generator,
return next yielded value or raise StopIteration."""
        return _athrow_send(self, value)

    def throw(self, typ, val=MISSING, tb=MISSING, /):
        return _athrow_throw(self, typ, val, tb)

    def close(self):
        """close() -> raise GeneratorExit inside generator."""
        return _awaitable_close(self, _athrow_throw)


# sys.set_asyncgen_hooks (sys.py installs it)

def set_asyncgen_hooks(firstiter=MISSING, finalizer=MISSING):
    """Set a finalizer for async generators objects."""
    if finalizer is not MISSING and finalizer is not None and not is_callable(finalizer):
        error(TypeError, str_join('', ('callable finalizer expected, got ', _name(finalizer))))
    if firstiter is not MISSING and firstiter is not None and not is_callable(firstiter):
        error(TypeError, str_join('', ('callable firstiter expected, got ', _name(firstiter))))
    _set_hooks(firstiter, finalizer)


kernel_set('send', send)
kernel_set('get_awaitable', get_awaitable)
kernel_set('get_aiter', get_aiter)
kernel_set('get_anext', get_anext)
kernel_set('stopiteration_error', stopiteration_error)
kernel_set('gen_close', _close)
