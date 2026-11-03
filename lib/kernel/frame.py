"""The frame type's methods and FrameLocalsProxy (Objects/frameobject.c,
PEP 667). The frame objects, their attributes and the fast locals are native
(runtime/interp/frame.c); the proxy reads and writes the frame's fast locals
and cells through primitives, and keeps the names that are not locals in a
dict on the frame object.
"""

from _lucifer import (
    MISSING, error, str_join, static_type, address, alloc, field_get, field_set, repr_enter,
    repr_leave, is_frame, frame_clear, frame_fast_get, frame_fast_set, frame_fast_items,
    frame_extra,
)
from _lucifer import _boot

kernel_type = _boot.kernel_type

__all__ = ()

_FrameLocalsProxy = static_type('FrameLocalsProxy')


@kernel_type(static_type('frame'))
class frame:
    def clear(self):
        """Clear all references held by the frame."""
        frame_clear(self)

    def __repr__(self):
        code = self.f_code
        return str_join('', ('<frame at ', address(self), ', file ', repr(code.co_filename),
                             ', line ', str(self.f_lineno), ', code ', code.co_name, '>'))


def _key_error(key):
    error(KeyError, str_join('', ("local variable '", repr(key), "' is not defined")))


def _get(self, key):
    frame = field_get(self, 0)
    value = frame_fast_get(frame, key)
    if value is not MISSING:
        return value
    extra = frame_extra(frame)
    if key in extra:
        return extra[key]
    _key_error(key)


def _set(self, key, value):
    frame = field_get(self, 0)
    if not frame_fast_set(frame, key, value):
        frame_extra(frame)[key] = value


def _items(self):
    return frame_fast_items(field_get(self, 0)) + list(frame_extra(field_get(self, 0)).items())


def _mergeable(other):
    return isinstance(other, dict) or isinstance(other, _FrameLocalsProxy)


def _merge(self, other):
    for key in other.keys():
        _set(self, key, other[key])


@kernel_type(_FrameLocalsProxy)
class FrameLocalsProxy:
    def __new__(cls, *args, **kwargs):
        if len(args) != 1:
            error(TypeError, str_join('', ('FrameLocalsProxy expected 1 argument, got ',
                                           str(len(args)))))
        if not is_frame(args[0]):
            error(TypeError, str_join('', ('expect frame, not ', type(args[0]).__name__)))
        if kwargs:
            error(TypeError, 'FrameLocalsProxy takes no keyword arguments')
        self = alloc(_FrameLocalsProxy, cls)
        field_set(self, 0, args[0])
        return self

    def __getitem__(self, key):
        return _get(self, key)

    def __setitem__(self, key, value):
        _set(self, key, value)

    def __delitem__(self, key):
        frame = field_get(self, 0)
        frame_fast_set(frame, key, MISSING)
        del frame_extra(frame)[key]

    def __contains__(self, key):
        frame = field_get(self, 0)
        return frame_fast_get(frame, key) is not MISSING or key in frame_extra(frame)

    def __len__(self):
        return len(_items(self))

    def __iter__(self):
        return iter(self.keys())

    def __reversed__(self):
        return self.keys()[::-1]

    def keys(self):
        return [k for k, v in _items(self)]

    def values(self):
        return [v for k, v in _items(self)]

    def items(self):
        return _items(self)

    def copy(self):
        return dict(_items(self))

    def get(self, *args):
        if not 1 <= len(args) <= 2:
            error(TypeError, 'get expected 1 or 2 arguments')
        value = frame_fast_get(field_get(self, 0), args[0])
        if value is not MISSING:
            return value
        extra = frame_extra(field_get(self, 0))
        if args[0] in extra:
            return extra[args[0]]
        return args[1] if len(args) == 2 else None

    def setdefault(self, *args):
        if not 1 <= len(args) <= 2:
            error(TypeError, 'setdefault expected 1 or 2 arguments')
        try:
            return _get(self, args[0])
        except KeyError:
            default = args[1] if len(args) == 2 else None
            _set(self, args[0], default)
            return default

    def pop(self, *args):
        if len(args) < 1:
            error(TypeError, 'pop expected at least 1 argument, got 0')
        if len(args) > 2:
            error(TypeError, str_join('', ('pop expected at most 2 arguments, got ', str(len(args)))))
        frame = field_get(self, 0)
        frame_fast_set(frame, args[0], MISSING)
        extra = frame_extra(frame)
        if args[0] in extra:
            return extra.pop(args[0])
        if len(args) == 2:
            return args[1]
        raise KeyError(args[0])

    def update(self, *args, **kwargs):
        if kwargs:
            error(TypeError, 'FrameLocalsProxy.update() takes no keyword arguments')
        if len(args) != 1:
            error(TypeError, str_join('', ('FrameLocalsProxy.update() takes exactly one argument (',
                                           str(len(args)), ' given)')))
        if not _mergeable(args[0]):
            error(TypeError, 'update() argument must be dict or another FrameLocalsProxy')
        _merge(self, args[0])

    def __or__(self, other):
        if not _mergeable(other):
            return NotImplemented
        result = dict(_items(self))
        result.update(other)
        return result

    def __ror__(self, other):
        if not _mergeable(other):
            return NotImplemented
        result = dict(other)
        result.update(_items(self))
        return result

    def __ior__(self, other):
        if not _mergeable(other):
            return NotImplemented
        _merge(self, other)
        return self

    def __eq__(self, other):
        if isinstance(other, _FrameLocalsProxy):
            return field_get(self, 0) is field_get(other, 0)
        if isinstance(other, dict):
            return dict(_items(self)) == other
        return NotImplemented

    def __ne__(self, other):
        result = self.__eq__(other)
        return result if result is NotImplemented else not result

    __hash__ = None

    def __repr__(self):
        if repr_enter(self):
            return '{...}'
        try:
            return repr(dict(_items(self)))
        finally:
            repr_leave(self)
