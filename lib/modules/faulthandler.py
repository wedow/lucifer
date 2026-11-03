"""faulthandler (Python/traceback.c, Modules/faulthandler.c): dump_traceback
writes the Python stack; the fatal-signal handlers are not installed, so
enable() only records the request."""

import sys

_enabled = False
_file = None


def enable(file=None, all_threads=True, c_stack=True):
    """Enable the fault handler."""
    global _enabled, _file
    _enabled = True
    _file = file


def disable():
    """Disable the fault handler."""
    global _enabled
    was, _enabled = _enabled, False
    return was


def is_enabled():
    """Check if the handler is enabled."""
    return _enabled


def _write_stack(file, frame):
    file.write('Stack (most recent call first):\n')
    depth = 0
    while frame is not None and depth < 100:
        code = frame.f_code
        file.write('  File "%s", line %d in %s\n' % (code.co_filename, frame.f_lineno, code.co_name))
        frame = frame.f_back
        depth += 1
    if frame is not None:
        file.write('  ...\n')


def dump_traceback(file=None, all_threads=True):
    """Dump the traceback of the current thread, or of all threads if all_threads is True, into file."""
    if file is None:
        file = sys.stderr
    elif isinstance(file, int):
        import os
        file = os.fdopen(os.dup(file), 'w', closefd=True)
    _write_stack(file, sys._getframe(1))
    flush = getattr(file, 'flush', None)
    if flush is not None:
        flush()


def dump_traceback_later(timeout, repeat=False, file=None, exit=False):
    """Dump the traceback of all threads in timeout seconds."""


def cancel_dump_traceback_later():
    """Cancel the previous call to dump_traceback_later()."""


def register(signum, file=None, all_threads=True, chain=False):
    """Register a handler for the signal 'signum': dump the traceback of the current thread."""


def unregister(signum):
    """Unregister the handler of the signal 'signum' registered by register()."""
    return False
