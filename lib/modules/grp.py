"""grp (Modules/grpmodule.c): the group database."""

import _lucifer

struct_group = _lucifer.structseq.structseq_type(
    'grp.struct_group', 'grp.struct_group: Results from getgr*() routines.\n\n'
    'This object may be accessed either as a tuple of\n'
    '  (gr_name,gr_passwd,gr_gid,gr_mem)\n'
    'or via the object attributes as named in the above tuple.',
    ('gr_name', 'gr_passwd', 'gr_gid', 'gr_mem'), 4)


def getgrgid(id):
    """Return the group database entry for the given numeric group ID."""
    if not isinstance(id, int):
        try:
            id = id.__index__()
        except AttributeError:
            raise TypeError("'%s' object cannot be interpreted as an integer" % type(id).__name__) from None
    entry = _lucifer.grp_gid(id) if -1 <= id < 2**32 else None
    if entry is None:
        raise KeyError('getgrgid(): gid not found: %d' % id)
    return struct_group(entry)


def getgrnam(name):
    """Return the group database entry for the given group name."""
    if not isinstance(name, str):
        raise TypeError("getgrnam() argument 'name' must be str, not %s" % type(name).__name__)
    if '\0' in name:
        raise ValueError('embedded null byte')
    entry = _lucifer.grp_name(name)
    if entry is None:
        raise KeyError('getgrnam(): name not found: %r' % name)
    return struct_group(entry)


def getgrall():
    """Return a list of all available group entries, in arbitrary order."""
    return [struct_group(entry) for entry in _lucifer.grp_all()]
