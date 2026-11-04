"""pwd (Modules/pwdmodule.c): the user database."""

import _lucifer

struct_passwd = _lucifer.structseq.structseq_type(
    'pwd.struct_passwd', 'pwd.struct_passwd: Results from getpw*() routines.\n\n'
    'This object may be accessed either as a tuple of\n'
    '  (pw_name,pw_passwd,pw_uid,pw_gid,pw_gecos,pw_dir,pw_shell)\n'
    'or via the object attributes as named in the above tuple.',
    ('pw_name', 'pw_passwd', 'pw_uid', 'pw_gid', 'pw_gecos', 'pw_dir', 'pw_shell'), 7)


def getpwuid(uid, /):
    """Return the password database entry for the given numeric user ID."""
    if not isinstance(uid, int):
        try:
            uid = uid.__index__()
        except AttributeError:
            raise TypeError('uid should be integer, not %s' % type(uid).__name__) from None
    entry = _lucifer.pwd_uid(uid) if -1 <= uid < 2**32 else None
    if entry is None:
        raise KeyError('getpwuid(): uid not found: %d' % uid)
    return struct_passwd(entry)


def getpwnam(name, /):
    """Return the password database entry for the given user name."""
    if not isinstance(name, str):
        raise TypeError('getpwnam() argument must be str, not %s' % type(name).__name__)
    if '\0' in name:
        raise ValueError('embedded null byte')
    entry = _lucifer.pwd_name(name)
    if entry is None:
        raise KeyError('getpwnam(): name not found: %r' % name)
    return struct_passwd(entry)


def getpwall():
    """Return a list of all available password database entries, in arbitrary order."""
    return [struct_passwd(entry) for entry in _lucifer.pwd_all()]
