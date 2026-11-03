"""gc (Modules/gcmodule.c): the collector's controls. Collection arrives with
the cycle collector; until then collect() finds nothing and the counts are zero."""

DEBUG_STATS = 1
DEBUG_COLLECTABLE = 2
DEBUG_UNCOLLECTABLE = 4
DEBUG_SAVEALL = 32
DEBUG_LEAK = DEBUG_COLLECTABLE | DEBUG_UNCOLLECTABLE | DEBUG_SAVEALL

garbage = []
callbacks = []

_enabled = True
_debug = 0
_threshold = (2000, 10, 10)
_frozen = 0


def enable():
    """Enable automatic garbage collection."""
    global _enabled
    _enabled = True


def disable():
    """Disable automatic garbage collection."""
    global _enabled
    _enabled = False


def isenabled():
    """Returns true if automatic garbage collection is enabled."""
    return _enabled


def collect(generation=2):
    """Run the garbage collector.

With no arguments, run a full collection.  The optional argument
may be an integer specifying which generation to collect.  A ValueError
is raised if the generation number is invalid.

The number of unreachable objects is returned."""
    if not 0 <= generation <= 2:
        raise ValueError('invalid generation')
    return 0


def get_threshold():
    """Return the current collection thresholds."""
    return _threshold


def set_threshold(threshold0, threshold1=None, threshold2=None, /):
    """Set the collection thresholds (the collection frequency)."""
    global _threshold
    _threshold = (threshold0,
                  _threshold[1] if threshold1 is None else threshold1,
                  _threshold[2] if threshold2 is None else threshold2)


def get_count():
    """Return a three-tuple of the current collection counts."""
    return (0, 0, 0)


def get_debug():
    """Get the garbage collection debugging flags."""
    return _debug


def set_debug(flags, /):
    """Set the garbage collection debugging flags."""
    global _debug
    _debug = flags


def get_stats():
    """Return a list of dictionaries containing per-generation statistics."""
    return [{'collections': 0, 'collected': 0, 'uncollectable': 0} for _ in range(3)]


def freeze():
    """Freeze all current tracked objects and ignore them for future collections."""


def unfreeze():
    """Unfreeze all objects in the permanent generation."""


def get_freeze_count():
    """Return the number of objects in the permanent generation."""
    return _frozen
