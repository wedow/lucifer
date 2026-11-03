"""atexit (Modules/atexitmodule.c): functions run at the interpreter's exit,
last registered first, by Py_FinalizeEx."""

import _lucifer

_callbacks = []


def register(func, /, *args, **kwargs):
    """Register a function to be executed upon normal program termination

    func - function to be called at exit
    args - optional arguments to pass to func
    kwargs - optional keyword arguments to pass to func

    func is returned to facilitate usage as a decorator."""
    if not callable(func):
        raise TypeError('the first argument must be callable')
    _callbacks.append((func, args, kwargs))
    return func


def unregister(func, /):
    """Unregister an exit function which was previously registered using
atexit.register

    func - function to be unregistered"""
    _callbacks[:] = [entry for entry in _callbacks if not entry[0] == func]


def _ncallbacks():
    """Return the number of registered exit functions."""
    return len(_callbacks)


def _clear():
    """Clear the list of previously registered exit functions."""
    del _callbacks[:]


def _run_exitfuncs():
    """Run all registered exit functions.

If a callback raises an exception, it is printed and the rest still run."""
    for i in range(len(_callbacks) - 1, -1, -1):
        func, args, kwargs = _callbacks[i]
        try:
            func(*args, **kwargs)
        except SystemExit:
            pass
        except BaseException as exc:
            exc.__traceback__ = exc.__traceback__.tb_next      # not this frame
            _lucifer.write_unraisable('Exception ignored in atexit callback', func, exc)
    del _callbacks[:]
