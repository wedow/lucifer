"""marshal (Python/marshal.c): reading and writing code objects and other
constants, from and to bytes and files."""

from _lucifer import marshal_dumps as _dumps, marshal_loads as _loads

version = 5


def loads(data, /, *, allow_code=True):
    """Convert the bytes-like object to a value."""
    return _loads(data if type(data) is bytes else bytes(data))[0]


def load(file, /, *, allow_code=True):
    """Read one value from the open file: the rest of it is read, and a seekable file is left just
after the value (CPython reads no further than the value)."""
    data = file.read()
    if not isinstance(data, bytes):
        raise TypeError('file.read() returned not bytes but %.100s' % type(data).__name__)
    value, used = _loads(data)
    if used < len(data):
        file.seek(used - len(data), 1)
    return value


def dumps(value, version=version, /, *, allow_code=True):
    """Return the bytes object that would be written to a file by dump(value, file)."""
    return _dumps(value, version, allow_code)


def dump(value, file, version=version, /, *, allow_code=True):
    """Write the value on the open file: dumps(value), written by one file.write call."""
    file.write(_dumps(value, version, allow_code))
