"""An exception out of __del__ is reported, not raised: the default
unraisablehook prints the deallocator header, then the traceback and the
exception line. A custom hook gets the exception itself. The finalizers are
callables with a fixed repr, as the header names them."""
import sys


class Finalizer:
    def __init__(self, body):
        self.body = body

    def __repr__(self):
        return '<finalizer ' + self.body.__name__ + '>'

    def __call__(self):
        self.body()


def boom():
    raise ValueError('boom')


def deep():
    sys._getframe(50)


class Raises:
    __del__ = Finalizer(boom)


class Deep:
    __del__ = Finalizer(deep)


x = Raises()
del x
print('after Raises', flush=True)
y = Deep()
del y
print('after Deep', flush=True)

seen = []
sys.unraisablehook = lambda u: seen.append((u.exc_type.__name__, str(u.exc_value), u.err_msg, repr(u.object)))
z = Raises()
del z
sys.unraisablehook = sys.__unraisablehook__
print(seen)
