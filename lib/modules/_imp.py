"""_imp (Python/import.c, Python/clinic/import.c.h): the surface importlib's
bootstrap uses, over the builtin and frozen tables of the runtime.

Builtin modules are the ports in lib/modules and the three native ones.
Frozen modules are the importlib bootstrap. Extension modules arrive with
the C-API loader.
"""

import _lucifer
import sys

check_hash_based_pycs = 'default'
pyc_magic_number_token = 3627 | (ord('\r') << 16) | (ord('\n') << 24)

_lock = None
_lock_count = 0


def acquire_lock():
    """Acquires the interpreter's import lock for the current thread.

This lock should be used by import hooks to ensure thread-safety when importing
modules. On platforms without threads, this function does nothing."""
    global _lock, _lock_count
    if _lock is None:
        import _thread
        _lock = _thread.RLock()
    _lock.acquire()
    _lock_count += 1


def release_lock():
    """Release the interpreter's import lock.

On platforms without threads, this function does nothing."""
    global _lock_count
    if _lock_count == 0:
        raise RuntimeError('not holding the import lock')
    _lock_count -= 1
    _lock.release()


def lock_held():
    """Return True if the import lock is currently held, else False.

On platforms without threads, return False."""
    return _lock_count > 0


def is_builtin(name, /):
    """Returns True if the module name corresponds to a built-in module."""
    return 1 if _lucifer.port_exists(name) else 0


def create_builtin(spec, /):
    """Create an extension module."""
    name = spec.name
    if not isinstance(name, str):
        raise TypeError('name must be string, not ' + type(name).__name__)
    return _lucifer.port_create(name)


def exec_builtin(mod, /):
    """Initialize a built-in module."""
    _lucifer.port_exec(mod)
    return 0


def is_frozen(name, /):
    """Returns True if the module name corresponds to a frozen module."""
    return _lucifer.frozen_find(name) is not None


def is_frozen_package(name, /):
    """Returns True if the module name is of a frozen package."""
    if _lucifer.frozen_find(name) is None:
        raise ImportError('No such frozen object named %r' % (name,), name=name)
    return False


def find_frozen(name, /, *, withdata=False):
    """Return info about the corresponding frozen module (if there is one) or None.

The returned info (a 2-tuple):

 * data         the raw marshalled bytes
 * is_package   whether or not it is a package
 * origname     the originally frozen module's name, or None if not
                a stdlib module (this will usually be the same as
                the module's current name)"""
    origname = _lucifer.frozen_find(name)
    if origname is None:
        return None
    return (None, False, origname)


def get_frozen_object(name, data=None, /):
    """Create a code object for a frozen module."""
    code = _lucifer.frozen_code(name)
    if code is None:
        raise ImportError('No such frozen object named %r' % (name,), name=name)
    return code


def init_frozen(name, /):
    """Initializes a frozen module."""
    if _lucifer.frozen_find(name) is None:
        return None
    module = type(sys)(name)
    exec(get_frozen_object(name), module.__dict__)
    sys.modules[name] = module
    return module


def _frozen_module_names():
    """Returns a list of frozen module names."""
    return list(_lucifer.frozen_names())


def _fix_co_filename(code, path, /):
    """Changes code.co_filename to specify the passed-in file path."""
    _lucifer.fix_co_filename(code, path)


def extension_suffixes():
    """Returns the list of file suffixes used to identify extension modules."""
    return []


def source_hash(key, source):
    raise NotImplementedError('_imp.source_hash needs the hash-based pyc support')


def _override_frozen_modules_for_tests(override, /):
    """(internal-only) Override PyConfig.use_frozen_modules."""


def _override_multi_interp_extensions_check(override, /):
    """(internal-only) Override PyInterpreterState.override_multi_interp_extensions_check."""
    return -1
