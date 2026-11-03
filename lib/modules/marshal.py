"""marshal (Python/marshal.c): reading and writing code objects and other
constants, from and to bytes."""

from _lucifer import marshal_dumps as _dumps, marshal_loads as _loads

version = 5


def loads(data, /, *, allow_code=True):
    """Convert the bytes-like object to a value."""
    return _loads(data if type(data) is bytes else bytes(data))


def dumps(value, version=version, /, *, allow_code=True):
    """Return the bytes object that would be written to a file by dump(value, file)."""
    return _dumps(value, version, allow_code)
