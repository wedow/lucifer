"""The partial accelerator, adapted from CPython's Lib/functools.py."""

from _lucifer import instance_dict as _instance_dict
from operator import itemgetter
from reprlib import recursive_repr
from types import GenericAlias, MethodType


class _PlaceholderType:
    """The type of the Placeholder singleton.

    Used as a placeholder for partial arguments.
    """
    __module__ = "functools"
    __instance = None
    __slots__ = ()

    def __init_subclass__(cls, *args, **kwargs):
        raise TypeError(f"type '{cls.__name__}' is not an acceptable base type")

    def __new__(cls):
        if cls.__instance is None:
            cls.__instance = object.__new__(cls)
        return cls.__instance

    def __repr__(self):
        return 'Placeholder'

    def __reduce__(self):
        return 'Placeholder'


Placeholder = _PlaceholderType()


def _partial_prepare_merger(args):
    if not args:
        return 0, None
    nargs = len(args)
    order = []
    j = nargs
    for i, a in enumerate(args):
        if a is Placeholder:
            order.append(j)
            j += 1
        else:
            order.append(i)
    phcount = j - nargs
    merger = itemgetter(*order) if phcount else None
    return phcount, merger


def _partial_new(cls, func, /, *args, **keywords):
    if not callable(func):
        raise TypeError("the first argument must be callable")
    if args and args[-1] is Placeholder:
        raise TypeError("trailing Placeholders are not allowed")
    for value in keywords.values():
        if value is Placeholder:
            raise TypeError("Placeholder cannot be passed as a keyword argument")
    # An existing namespace, even an empty one, keeps the inner partial intact.
    if issubclass(type(func), partial) and _instance_dict(func, None) is None:
        pto_phcount = func._phcount
        tot_args = func.args
        if args:
            tot_args += args
            if pto_phcount:
                # Fill the inner partial's placeholders from the new arguments.
                nargs = len(args)
                if nargs < pto_phcount:
                    tot_args += (Placeholder,) * (pto_phcount - nargs)
                tot_args = func._merger(tot_args)
                if nargs > pto_phcount:
                    tot_args += args[pto_phcount:]
            phcount, merger = _partial_prepare_merger(tot_args)
        else:
            phcount, merger = pto_phcount, func._merger
        keywords = {**func.keywords, **keywords}
        func = func.func
    else:
        tot_args = args
        phcount, merger = _partial_prepare_merger(tot_args)

    self = object.__new__(cls)
    self.func = func
    self.args = tot_args
    self.keywords = keywords
    self._phcount = phcount
    self._merger = merger
    return self


def _partial_repr(self):
    cls = type(self)
    module = cls.__module__
    qualname = cls.__qualname__
    args = [repr(self.func)]
    args.extend(map(repr, self.args))
    args.extend(f"{k}={v!r}" for k, v in self.keywords.items())
    return f"{module}.{qualname}({', '.join(args)})"


class partial:
    """New function with partial application of the given arguments
    and keywords.
    """

    __module__ = "functools"
    __slots__ = ("func", "args", "keywords", "_phcount", "_merger",
                 "__dict__", "__weakref__")

    __new__ = _partial_new
    __repr__ = recursive_repr()(_partial_repr)

    def __call__(self, /, *args, **keywords):
        phcount = self._phcount
        if phcount:
            try:
                pto_args = self._merger(self.args + args)
                args = args[phcount:]
            except IndexError:
                raise TypeError("missing positional arguments "
                                "in 'partial' call; expected "
                                f"at least {phcount}, got {len(args)}")
        else:
            pto_args = self.args
        keywords = {**self.keywords, **keywords}
        return self.func(*pto_args, *args, **keywords)

    def __get__(self, obj, objtype=None):
        if obj is None:
            return self
        return MethodType(self, obj)

    def __reduce__(self):
        state = (self.func, self.args, self.keywords, _instance_dict(self, None))
        return type(self), (self.func,), state

    def __setstate__(self, state):
        if not isinstance(state, tuple) or len(state) != 4:
            raise TypeError("invalid partial state")
        func, args, kwds, namespace = state
        if (not callable(func) or not isinstance(args, tuple) or
            (kwds is not None and not isinstance(kwds, dict)) or
            (namespace is not None and not isinstance(namespace, dict))):
            raise TypeError("invalid partial state")

        if args and args[-1] is Placeholder:
            raise TypeError("trailing Placeholders are not allowed")
        phcount, merger = _partial_prepare_merger(args)

        args = tuple(args)
        if kwds is None:
            kwds = {}
        elif type(kwds) is not dict:
            kwds = dict(kwds)
        if namespace is None:
            del self.__dict__
        else:
            self.__dict__ = namespace
        self.func = func
        self.args = args
        self.keywords = kwds
        self._phcount = phcount
        self._merger = merger

    __class_getitem__ = classmethod(GenericAlias)
