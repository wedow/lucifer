"""The module type (Objects/moduleobject.c): construction, repr, dir and the
attribute lookup that falls back to the module's __getattr__ (PEP 562)."""

from _lucifer import (
    error, attribute_error, str_join, kind, static_type, alloc, instance_dict, ns_get, module_init, import_state,
    kernel_get, MISSING, type_dict,
)
from _lucifer import _boot, object as _object

kernel_type = _boot.kernel_type
generic_getattr = kernel_get('generic_getattr')

__all__ = ()


def _initializing(spec):
    """_PyModuleSpec_IsInitializing."""
    if spec is None:
        return False
    try:
        return bool(spec._initializing)
    except AttributeError:
        return False


def _module_getattr(self, name, suppress=False):
    """module_getattro: generic lookup, then the module's __getattr__; MISSING
    instead of AttributeError when suppress."""
    try:
        v = generic_getattr(self, name, True)
    except AttributeError:
        v = MISSING
    if v is not MISSING:
        return v
    d = instance_dict(self, True)
    hook = ns_get(d, '__getattr__', MISSING)
    if hook is not MISSING:
        return hook(name)
    if suppress:
        return MISSING
    module_name = ns_get(d, '__name__', MISSING)
    if kind(module_name, 'str'):
        spec = ns_get(d, '__spec__', None)
        if _initializing(spec):
            text = str_join('', ("partially initialized module '", module_name,
                                 "' has no attribute '", name,
                                 "' (most likely due to a circular import)"))
        else:
            text = str_join('', ("module '", module_name, "' has no attribute '", name, "'"))
    else:
        text = str_join('', ("module has no attribute '", name, "'"))
    attribute_error(text, name, self)


@kernel_type(static_type('module'))
class module:
    """Create a module object.

The name must be a string; the optional doc argument can have any type."""

    def __new__(cls, /, *args, **kwargs):
        # new_module_notrack: the dict exists before __init__, so every
        # reader of md_dict may assume it.
        self = alloc(cls, module)
        instance_dict(self, True)
        return self

    def __init__(self, name, doc=None):
        if not kind(name, 'str'):
            error(TypeError, str_join('', ('module.__init__() argument \'name\' must be str, not ',
                                           type(name).__name__)))
        module_init(self, name, doc)

    def __repr__(self):
        importlib = import_state()[0]
        if importlib is None:
            name = instance_dict(self, True).get('__name__', '?')
            return str_join('', ("<module '", name, "'>"))
        return importlib._module_repr(self)

    def __getattribute__(self, name):
        return _module_getattr(self, name)

    def __dir__(self):
        d = instance_dict(self, True)
        hook = ns_get(d, '__dir__', MISSING)
        if hook is not MISSING:
            return hook()
        return list(d)


_object.module_getattribute = ns_get(type_dict(module), '__getattribute__', MISSING)
_object.module_getattr = _module_getattr
