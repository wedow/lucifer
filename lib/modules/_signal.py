"""_signal (Modules/signalmodule.c): the signal numbers and handler table.
A Python handler is installed with the runtime's sigaction (runtime/interp/
signals.c), which only records the signal: the handler runs in the main task
at its next poll. Importing this module installs SIGINT's default handler, unless
the process ignores SIGINT, as a background job does: the inherited SIG_IGN is
recorded instead; the runtime's startup does not import it."""

import _lucifer

ITIMER_PROF = 2
ITIMER_REAL = 0
ITIMER_VIRTUAL = 1
NSIG = 65
SIGABRT = 6
SIGALRM = 14
SIGBUS = 7
SIGCHLD = 17
SIGCLD = 17
SIGCONT = 18
SIGFPE = 8
SIGHUP = 1
SIGILL = 4
SIGINT = 2
SIGIO = 29
SIGIOT = 6
SIGKILL = 9
SIGPIPE = 13
SIGPOLL = 29
SIGPROF = 27
SIGPWR = 30
SIGQUIT = 3
SIGRTMAX = 64
SIGRTMIN = 34
SIGSEGV = 11
SIGSTKFLT = 16
SIGSTOP = 19
SIGSYS = 31
SIGTERM = 15
SIGTRAP = 5
SIGTSTP = 20
SIGTTIN = 21
SIGTTOU = 22
SIGURG = 23
SIGUSR1 = 10
SIGUSR2 = 12
SIGVTALRM = 26
SIGWINCH = 28
SIGXCPU = 24
SIGXFSZ = 25
SIG_BLOCK = 0
SIG_DFL = 0
SIG_IGN = 1
SIG_SETMASK = 2
SIG_UNBLOCK = 1

SIG_DFL = 0
SIG_IGN = 1

_handlers = {}


def default_int_handler(signalnum, frame, /):
    """The default handler for SIGINT installed by Python.

It raises KeyboardInterrupt."""
    raise KeyboardInterrupt


def valid_signals():
    """Return a set of valid signal numbers on this platform."""
    return set(range(1, NSIG)) - {32, 33}


def _check(signalnum):
    if not isinstance(signalnum, int):
        raise TypeError("'%s' object cannot be interpreted as an integer" % type(signalnum).__name__)
    if not 1 <= signalnum < NSIG:
        raise ValueError('signal number out of range')


def getsignal(signalnum, /):
    """Return the current action for the given signal.

The return value can be:
  SIG_IGN -- if the signal is being ignored
  SIG_DFL -- if the default action for the signal is in effect
  None    -- if an unknown handler is in effect
  anything else -- the callable Python object used as a handler"""
    _check(signalnum)
    return _handlers.get(signalnum, default_int_handler if signalnum == SIGINT else SIG_DFL)


def signal(signalnum, handler, /):
    """Set the action for the given signal.

The action can be SIG_DFL, SIG_IGN, or a callable Python object.
The previous action is returned.  See getsignal() for possible return values."""
    _check(signalnum)
    if handler not in (SIG_DFL, SIG_IGN) and not callable(handler):
        raise TypeError('signal handler must be signal.SIG_IGN, signal.SIG_DFL, or a callable object')
    old = getsignal(signalnum)
    _lucifer.signal_install(signalnum, handler)
    _handlers[signalnum] = handler
    return old


def strsignal(signalnum, /):
    """Return the system description of the signal signalnum."""
    _check(signalnum)
    return None


def raise_signal(signalnum, /):
    """Send a signal to the executing process."""
    _check(signalnum)
    _lucifer.signal_raise(signalnum)


if _lucifer.signal_ignored(SIGINT):
    _handlers[SIGINT] = SIG_IGN
else:
    _lucifer.signal_install(SIGINT, default_int_handler)


def alarm(seconds, /):
    """Arrange for SIGALRM to arrive after the given number of seconds."""
    return _lucifer.os_call('alarm', seconds)


class ItimerError(OSError):
    pass


def _timer(seconds):
    if seconds < 0:
        raise ItimerError(22, 'Invalid argument')
    return round(seconds * 1e6)


def setitimer(which, seconds, interval=0.0, /):
    """Sets given itimer (one of ITIMER_REAL, ITIMER_VIRTUAL or ITIMER_PROF) especially
to seconds, with an interval, and returns the old (seconds, interval) tuple."""
    old = _lucifer.os_itimer(which, _timer(seconds), _timer(interval))
    return old[0] / 1e6, old[1] / 1e6


def getitimer(which, /):
    """Returns current value of given itimer."""
    old = _lucifer.os_itimer(which, -1, 0)
    return old[0] / 1e6, old[1] / 1e6


def pthread_sigmask(how, mask, /):
    """Fetch and/or change the signal mask of the calling thread."""
    return set(_lucifer.os_pthread_sigmask(how, sorted(mask)))


def pthread_kill(thread_id, signalnum, /):
    """Send a signal to a thread."""
    _lucifer.os_pthread_kill(thread_id, signalnum)
