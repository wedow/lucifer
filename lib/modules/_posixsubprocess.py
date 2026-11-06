"""_posixsubprocess (Modules/_posixsubprocess.c): fork_exec over the
os_fork_exec primitive. preexec_fn cannot run between fork and exec, since the
child may not call back into the interpreter."""

import sys

import _lucifer


def _bytes(value):
    if isinstance(value, bytes):
        return value
    if isinstance(value, str):
        return value.encode('utf-8', 'surrogateescape')
    if hasattr(value, '__fspath__'):
        return _bytes(value.__fspath__())
    raise TypeError('expected str, bytes or os.PathLike object, not %s' % type(value).__name__)


def _charp_array(seq):
    """_PySequence_BytesToCharpArray: each item of the sequence, indexed,
    as bytes, with the C's errors (an array of pointers too large to
    allocate is out of memory)."""
    size = len(seq)
    if size > (sys.maxsize - 8) // 8:
        raise MemoryError()
    if not hasattr(type(seq), '__getitem__'):
        raise TypeError("'%s' object does not support indexing" % type(seq).__name__)
    items = []
    for i in range(size):
        item = seq[i]
        if not isinstance(item, bytes):
            raise TypeError('expected bytes, %s found' % type(item).__name__)
        if b'\0' in item:
            raise ValueError('embedded null byte')
        items.append(item)
    return tuple(items)


def fork_exec(args, executable_list, close_fds, pass_fds, cwd, env, p2cread, p2cwrite, c2pread, c2pwrite,
              errread, errwrite, errpipe_read, errpipe_write, restore_signals, call_setsid, pgid_to_set,
              gid, extra_groups, uid, child_umask, preexec_fn, /):
    """Spawn a fresh process: fork, set up its descriptors, and exec."""
    if close_fds and errpipe_write < 3:
        raise ValueError('errpipe_write must be >= 3')
    exec_array = _charp_array(executable_list)
    if args is not None:
        try:
            args = tuple(args)
        except TypeError:
            raise TypeError('argv must be a tuple') from None
    argv = None if args is None else tuple(_bytes(a) for a in args)
    if preexec_fn is not None:
        raise RuntimeError('preexec_fn is not supported')
    return _lucifer.os_fork_exec(
        argv, exec_array, bool(close_fds), tuple(pass_fds),
        None if cwd is None else _bytes(cwd), None if env is None else tuple(_bytes(e) for e in env),
        p2cread, p2cwrite, c2pread, c2pwrite, errread, errwrite, errpipe_read, errpipe_write,
        bool(restore_signals), bool(call_setsid), pgid_to_set, -1 if gid is None else gid,
        None if extra_groups is None else tuple(extra_groups), -1 if uid is None else uid, child_umask)
