"""gc (Modules/gcmodule.c): the collector's controls. The settings and counts
live in the runtime (runtime/interp/gc.c), which schedules a collection at the
first call or back-edge after the allocation counts pass the threshold; the
cycle collector is runtime/interp/collector.c."""

import _lucifer

DEBUG_STATS = 1
DEBUG_COLLECTABLE = 2
DEBUG_UNCOLLECTABLE = 4
DEBUG_SAVEALL = 32
DEBUG_LEAK = DEBUG_COLLECTABLE | DEBUG_UNCOLLECTABLE | DEBUG_SAVEALL

garbage = []
callbacks = []

# _lucifer.gc_get/gc_set fields.
_ENABLED, _YOUNG, _OLD0, _OLD1, _COUNT_YOUNG, _COUNT_OLD0, _COUNT_OLD1, _DEBUG = range(8)


def _run_callbacks(phase, generation, collected, uncollectable):
    """Call each of callbacks with the phase and the collection's info, as the
runtime starts and ends a collection. A callback that raises is reported and
the rest still run."""
    info = {'generation': generation, 'collected': collected,
            'uncollectable': uncollectable}
    for callback in callbacks[:]:
        try:
            callback(phase, info)
        except BaseException as exc:
            exc.__traceback__ = exc.__traceback__.tb_next      # not this frame
            _lucifer.write_unraisable('Exception ignored while invoking gc callback', callback, exc)


_lucifer.gc_hook(_run_callbacks, garbage)


def enable():
    """Enable automatic garbage collection."""
    _lucifer.gc_set(_ENABLED, 1)


def disable():
    """Disable automatic garbage collection."""
    _lucifer.gc_set(_ENABLED, 0)


def isenabled():
    """Returns true if automatic garbage collection is enabled."""
    return _lucifer.gc_get(_ENABLED) != 0


def collect(generation=2):
    """Run the garbage collector.

With no arguments, run a full collection.  The optional argument
may be an integer specifying which generation to collect.  A ValueError
is raised if the generation number is invalid.

The number of unreachable objects is returned."""
    if not 0 <= generation <= 2:
        raise ValueError('invalid generation')
    return _lucifer.gc_collect(generation)


def get_threshold():
    """Return the current collection thresholds."""
    return (_lucifer.gc_get(_YOUNG), _lucifer.gc_get(_OLD0), _lucifer.gc_get(_OLD1))


def set_threshold(threshold0, threshold1=None, threshold2=None, /):
    """Set the collection thresholds (the collection frequency)."""
    _lucifer.gc_set(_YOUNG, threshold0)
    if threshold1 is not None:
        _lucifer.gc_set(_OLD0, threshold1)
    if threshold2 is not None:
        _lucifer.gc_set(_OLD1, threshold2)


def get_count():
    """Return a three-tuple of the current collection counts."""
    return (_lucifer.gc_get(_COUNT_YOUNG), _lucifer.gc_get(_COUNT_OLD0), _lucifer.gc_get(_COUNT_OLD1))


def get_debug():
    """Get the garbage collection debugging flags."""
    return _lucifer.gc_get(_DEBUG)


def set_debug(flags, /):
    """Set the garbage collection debugging flags."""
    _lucifer.gc_set(_DEBUG, flags)


def get_stats():
    """Return a list of dictionaries containing per-generation statistics."""
    flat = _lucifer.gc_stats()
    return [{'collections': flat[3 * i], 'collected': flat[3 * i + 1],
             'uncollectable': flat[3 * i + 2]} for i in range(3)]


def get_objects(generation=None):
    """Return a list of objects tracked by the collector (excluding the list returned).

If generation is not None, return only the objects tracked by the
collector that are in that generation."""
    if generation is not None:
        if not isinstance(generation, int):
            raise TypeError("'%s' object cannot be interpreted as an integer" % type(generation).__name__)
        if generation >= 3:
            raise ValueError('generation parameter must be less than the number of available generations (3)')
        if generation < 0:
            raise ValueError('generation parameter cannot be negative')
    return _lucifer.gc_get_objects()


def get_referrers(*objs):
    """Return the list of objects that directly refer to any of 'objs'."""
    return _lucifer.gc_get_referrers(objs)


def get_referents(*objs):
    """Return the list of objects directly referred to by 'objs'."""
    return _lucifer.gc_get_referents(objs)


def is_tracked(obj, /):
    """Returns true if the object is tracked by the garbage collector."""
    return _lucifer.gc_is_tracked(obj)


def is_finalized(obj, /):
    """Returns true if the object has been already finalized by the GC."""
    return _lucifer.gc_is_finalized(obj)


def freeze():
    """Freeze all current tracked objects and ignore them for future collections."""
    _lucifer.gc_freeze(1)


def unfreeze():
    """Unfreeze all objects in the permanent generation."""
    _lucifer.gc_freeze(0)


def get_freeze_count():
    """Return the number of objects in the permanent generation."""
    return _lucifer.gc_freeze(-1)
