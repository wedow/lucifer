"""_tracemalloc (Modules/_tracemalloc.c): allocation tracing. The runtime keeps
no per-allocation records yet, so tracing can be switched on and off and
reports nothing traced."""

_tracing = False
_limit = 1


def is_tracing():
    """Return True if the tracemalloc module is tracing Python memory allocations."""
    return _tracing


def start(nframe=1, /):
    """Start tracing Python memory allocations."""
    global _tracing, _limit
    if not isinstance(nframe, int):
        raise TypeError("'%s' object cannot be interpreted as an integer" % type(nframe).__name__)
    if not 1 <= nframe <= 0xFFFF:
        raise ValueError('the number of frames must be in range [1; 65535]')
    _tracing, _limit = True, nframe


def stop():
    """Stop tracing Python memory allocations and clear traces."""
    global _tracing
    _tracing = False


def clear_traces():
    """Clear traces of memory blocks allocated by Python."""


def get_traceback_limit():
    """Get the maximum number of frames stored in the traceback of a trace."""
    return _limit


def get_traced_memory():
    """Get the current size and peak size of memory blocks traced by tracemalloc."""
    return (0, 0)


def reset_peak():
    """Set the peak size of memory blocks traced by tracemalloc to the current size."""


def get_tracemalloc_memory():
    """Get the memory usage in bytes of the tracemalloc module."""
    return 0


def _get_object_traceback(obj, /):
    """Get the traceback where the Python object obj was allocated."""
    return None


def _get_traces():
    """Get traces of all memory blocks allocated by Python."""
    return []
