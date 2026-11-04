"""select (Modules/selectmodule.c): select() and poll() over the poll(2)
primitive. epoll and kqueue are absent, so selectors uses poll."""

import _lucifer
import time as _time
from operator import index as _index

error = OSError

POLLIN, POLLPRI, POLLOUT, POLLERR, POLLHUP, POLLNVAL = 1, 2, 4, 8, 16, 32
POLLRDNORM, POLLRDBAND, POLLWRNORM, POLLWRBAND, POLLMSG, POLLRDHUP = 64, 128, 256, 512, 1024, 8192
PIPE_BUF = 4096


def _fileno(obj):
    if isinstance(obj, int):
        fd = obj
    else:
        try:
            fd = obj.fileno()
        except AttributeError:
            raise TypeError('argument must be an int, or have a fileno() method.') from None
        fd = _index(fd)
    if fd < 0:
        raise ValueError('file descriptor cannot be a negative integer (%d)' % fd)
    return fd


def _wait(fds, timeout):
    """poll(2), retried after a signal with the time that is left."""
    deadline = None if timeout is None else _time.monotonic() + timeout
    while True:
        if deadline is None:
            ms = -1
        else:
            ms = max(0, -(-int((deadline - _time.monotonic()) * 1000000) // 1000))
        ready = _lucifer.os_poll(fds, ms)
        if ready is not None:
            return ready


def select(rlist, wlist, xlist, timeout=None, /):
    """Wait until one or more file descriptors are ready for some kind of I/O."""
    if timeout is not None:
        if not isinstance(timeout, (int, float)):
            raise TypeError("'%s' object cannot be interpreted as an integer or float" % type(timeout).__name__)
        if timeout < 0:
            raise ValueError('timeout must be non-negative')
    wanted = {}
    for objects, flag in ((rlist, POLLIN), (wlist, POLLOUT), (xlist, POLLPRI)):
        for obj in objects:
            wanted.setdefault(_fileno(obj), []).append((obj, flag))
    fds = [(fd, sum({flag for _, flag in entries})) for fd, entries in wanted.items()]
    ready = dict(_wait(fds, timeout))
    result = ([], [], [])
    for index, (flag, bad) in enumerate(((POLLIN, POLLIN | POLLHUP | POLLERR),
                                         (POLLOUT, POLLOUT | POLLERR), (POLLPRI, POLLPRI))):
        for fd, entries in wanted.items():
            events = ready.get(fd, 0)
            if events & POLLNVAL:
                raise OSError(9, 'Bad file descriptor')
            for obj, f in entries:
                if f == flag and events & bad:
                    result[index].append(obj)
    return result


# A builtin, not a function: kept as a class attribute (selectors.SelectSelector._select) it does not bind.
select = staticmethod(select)


class poll:
    """Returns a polling object."""

    def __init__(self):
        self._fds = {}

    def register(self, fd, eventmask=POLLIN | POLLPRI | POLLOUT, /):
        """Register a file descriptor with the polling object."""
        self._fds[_fileno(fd)] = eventmask

    def modify(self, fd, eventmask, /):
        """Modify an already registered file descriptor."""
        fd = _fileno(fd)
        if fd not in self._fds:
            raise FileNotFoundError(2, 'No such file or directory')
        self._fds[fd] = eventmask

    def unregister(self, fd, /):
        """Remove a file descriptor being tracked by the polling object."""
        try:
            del self._fds[_fileno(fd)]
        except KeyError:
            raise KeyError(fd) from None

    def poll(self, timeout=None, /):
        """Polls the set of registered file descriptors."""
        if timeout is not None:
            if not isinstance(timeout, (int, float)):
                raise TypeError("'%s' object cannot be interpreted as an integer" % type(timeout).__name__)
            timeout = None if timeout < 0 else timeout / 1000.0
        return _wait(list(self._fds.items()), timeout)
