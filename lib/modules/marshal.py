"""marshal: reading code objects and other constants. Writing arrives
with the compiler's need for it."""

from _lucifer import marshal_loads as _loads

version = 5


def loads(data, /, *, allow_code=True):
    """Convert the bytes-like object to a value."""
    return _loads(data if type(data) is bytes else bytes(data))


def dumps(value, version=version, /, *, allow_code=True):
    """Return the bytes object that would be written to a file by dump(value, file)."""
    raise NotImplementedError('marshal.dumps arrives with the writer')
