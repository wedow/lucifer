"""The part of _pickle that pickle.py cannot supply in Python: PickleBuffer.

The Pickler, Unpickler and functions stay in pickle.py (it falls back to its
own when they are missing here)."""


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
