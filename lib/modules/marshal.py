"""marshal (Python/marshal.c): reading and writing code objects and other
constants, from and to bytes and files."""

from _lucifer import marshal_dumps as _dumps, marshal_load as _load, marshal_loads as _loads

version = 5


def loads(data, /, *, allow_code=True):
    """Convert the bytes-like object to a value."""
    return _loads(data, allow_code)


def load(file, /, *, allow_code=True):
    """Read one value from the open file, reading no further than the value."""
    return _load(file, allow_code)


def dumps(value, version=version, /, *, allow_code=True):
    """Return the bytes object that would be written to a file by dump(value, file)."""
    return _dumps(value, version, allow_code)


def dump(value, file, version=version, /, *, allow_code=True):
    """Write the value on the open file: dumps(value), written by one file.write call."""
    file.write(_dumps(value, version, allow_code))
