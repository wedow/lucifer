"""faulthandler (Modules/faulthandler.c): dumps the Python stack, on a fatal
signal, on request, after a timeout or on a user signal. The dumping is
native (runtime/prim/faulthandler.c); this keeps CPython's argument handling."""

import sys
import _lucifer

_enabled_file = None
_later_file = None
_registered = {}
_FATAL_SIGNALS = (4, 6, 7, 8, 11)           # SIGILL, SIGABRT, SIGBUS, SIGFPE, SIGSEGV


def _target(file):
    """(fd, file) for a file argument: a descriptor, a file object, or sys.stderr."""
    if file is None:
        file = sys.stderr
        if file is None:
            raise RuntimeError('sys.stderr is None')
    if isinstance(file, int):
        return file, None
    fd = file.fileno()
    if not isinstance(fd, int) or fd < 0:
        raise ValueError('file.fileno() is not a valid file descriptor')
    try:
        file.flush()
    except Exception:
        pass
    return fd, file


def _flush(file):
    if file is not None:
        try:
            file.flush()
        except Exception:
            pass


def _extension_modules():
    """The line naming the builtin and extension modules a crash report lists."""
    stdlib = sys.stdlib_module_names
    names = [name for name in sys.builtin_module_names if name not in stdlib]
    for name, module in list(sys.modules.items()):
        path = getattr(module, '__file__', None)
        if isinstance(path, str) and path.endswith('.so') and name not in stdlib and name not in names:
            names.append(name)
    if not names:
        return None
    return 'Extension modules: %s (total: %d)' % (', '.join(sorted(names)), len(names))


def enable(file=None, all_threads=True, c_stack=True):
    """Enable the fault handler."""
    global _enabled_file
    fd, keep = _target(file)
    _enabled_file = keep
    _lucifer.fh_enable(fd, bool(all_threads), bool(c_stack), _extension_modules())


def disable():
    """Disable the fault handler."""
    global _enabled_file
    was = _lucifer.fh_disable()
    _enabled_file = None
    return was


def is_enabled():
    """Check if the handler is enabled."""
    return _lucifer.fh_is_enabled()


def dump_traceback(file=None, all_threads=True):
    """Dump the traceback of the current thread, or of all threads if all_threads is True, into file."""
    fd, keep = _target(file)
    _lucifer.fh_dump(fd, bool(all_threads))
    _flush(keep)


def dump_c_stack(file=None):
    """Dump the C stack of the current thread."""
    fd, keep = _target(file)
    _lucifer.fh_dump_c_stack(fd)
    _flush(keep)


def _timedelta_text(us):
    """str(datetime.timedelta(microseconds=us)) for the Timeout header."""
    seconds, micro = divmod(us, 1000000)
    minutes, seconds = divmod(seconds, 60)
    hours, minutes = divmod(minutes, 60)
    text = '%d:%02d:%02d' % (hours, minutes, seconds)
    return text + '.%06d' % micro if micro else text


def dump_traceback_later(timeout, repeat=False, file=None, exit=False):
    """Dump the traceback of all threads in timeout seconds,
or each timeout seconds if repeat is True. If exit is True, call _exit(1) which is not safe."""
    global _later_file
    seconds = float(timeout)
    if seconds != seconds or seconds <= 0:
        raise ValueError('timeout must be greater than 0')
    if seconds > 9223372036.0:
        raise OverflowError('timeout value is too large')
    us = int(seconds * 1000000)
    if us <= 0:
        raise ValueError('timeout must be greater than 0')
    fd, keep = _target(file)
    _later_file = keep
    _lucifer.fh_dump_later(us, bool(repeat), fd, bool(exit), 'Timeout (%s)!\n' % _timedelta_text(us))


def cancel_dump_traceback_later():
    """Cancel the previous call to dump_traceback_later()."""
    global _later_file
    _lucifer.fh_cancel_later()
    _later_file = None


def register(signum, file=None, all_threads=True, chain=False):
    """Register a handler for the signal 'signum': dump the traceback of the current thread."""
    if not isinstance(signum, int):
        raise TypeError("an integer is required (got type %s)" % type(signum).__name__)
    if signum in _FATAL_SIGNALS:
        raise RuntimeError('signal %d cannot be registered, use enable() instead' % signum)
    if not 1 <= signum < 65:
        raise ValueError('signal number out of range')
    fd, keep = _target(file)
    if not _lucifer.fh_register(signum, fd, bool(all_threads), bool(chain)):
        raise RuntimeError('sigaction failed')
    _registered[signum] = keep


def unregister(signum):
    """Unregister the handler of the signal 'signum' registered by register()."""
    if not isinstance(signum, int):
        raise TypeError("an integer is required (got type %s)" % type(signum).__name__)
    if not 1 <= signum < 65:
        raise ValueError('signal number out of range')
    _registered.pop(signum, None)
    return _lucifer.fh_unregister(signum)


def _read_null():
    _lucifer.fh_read_null()


def _sigsegv(release_gil=False):
    _lucifer.fh_sigsegv()


def _sigabrt():
    _lucifer.fh_sigabrt()


def _sigfpe():
    _lucifer.fh_sigfpe()


def _stack_overflow():
    _lucifer.fh_stack_overflow()


def _fatal_error_c_thread():
    _lucifer.fh_fatal_error_c_thread()
