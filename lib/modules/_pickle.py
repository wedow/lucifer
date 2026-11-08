"""Optimized C implementation for the Python pickle module.

Here the Pickler and Unpickler are pickle.py's own (_Pickler, _Unpickler),
made on first use: pickle imports this module before it defines them. What
pickle.py cannot supply is PickleBuffer and the exceptions, defined here."""


class PickleError(Exception):
    pass


class PicklingError(PickleError):
    pass


class UnpicklingError(PickleError):
    pass


def _classes():
    """(Pickler, Unpickler), subclasses of pickle.py's own, made on first use."""
    made = globals().get('Pickler')
    if made is not None:
        return made, globals()['Unpickler']
    import pickle

    class Pickler(pickle._Pickler):
        """This takes a binary file for writing a pickle data stream."""
        __module__, __qualname__ = '_pickle', 'Pickler'

        def __init__(self, file, protocol=None, fix_imports=True, buffer_callback=None):
            super().__init__(file, protocol, fix_imports=fix_imports, buffer_callback=buffer_callback)

    class Unpickler(pickle._Unpickler):
        """This takes a binary file for reading a pickle data stream."""
        __module__, __qualname__ = '_pickle', 'Unpickler'

    globals().update(Pickler=Pickler, Unpickler=Unpickler)
    return Pickler, Unpickler


def __getattr__(name):
    if name == 'Pickler':
        return _classes()[0]
    if name == 'Unpickler':
        return _classes()[1]
    raise AttributeError("module '_pickle' has no attribute %r" % name)


def dump(obj, file, protocol=None, *, fix_imports=True, buffer_callback=None):
    """Write a pickled representation of obj to the open file object file."""
    _classes()[0](file, protocol, fix_imports=fix_imports, buffer_callback=buffer_callback).dump(obj)


def dumps(obj, protocol=None, *, fix_imports=True, buffer_callback=None):
    """Return the pickled representation of the object as a bytes object."""
    import io
    f = io.BytesIO()
    _classes()[0](f, protocol, fix_imports=fix_imports, buffer_callback=buffer_callback).dump(obj)
    return f.getvalue()


def load(file, *, fix_imports=True, encoding='ASCII', errors='strict', buffers=()):
    """Read and return an object from the pickle data stored in a file."""
    return _classes()[1](file, fix_imports=fix_imports, encoding=encoding, errors=errors,
                         buffers=buffers).load()


def loads(data, /, *, fix_imports=True, encoding='ASCII', errors='strict', buffers=()):
    """Read and return an object from the given pickle data."""
    import io
    if isinstance(data, str):
        raise TypeError("Can't load pickle from unicode string")
    return _classes()[1](io.BytesIO(data), fix_imports=fix_imports, encoding=encoding, errors=errors,
                         buffers=buffers).load()


class PickleBuffer:
    """Wrapper for potentially out-of-band buffers"""

    __module__ = 'pickle'
    __slots__ = ('_view',)

    def __init__(self, buffer):
        try:
            self._view = memoryview(buffer)
        except TypeError:
            raise TypeError("a bytes-like object is required, not '%s'" % type(buffer).__name__) from None

    def _live(self):
        if self._view is None:
            raise ValueError("operation forbidden on released PickleBuffer object")
        return self._view

    def __buffer__(self, flags):
        return memoryview(self._live())

    def raw(self):
        """Return a memoryview of the raw memory underlying this buffer.
Will raise BufferError is the buffer isn't contiguous."""
        view = self._live()
        if not view.contiguous:
            raise BufferError("cannot extract raw buffer from non-contiguous buffer")
        return view.cast('B')

    def release(self):
        """Release the underlying buffer exposed by the PickleBuffer object."""
        if self._view is not None:
            self._view.release()
            self._view = None
