"""Exit-status mapping shared by the test drivers."""
import signal

FATAL_SIGNALS = {int(s) for s in (signal.SIGSEGV, signal.SIGABRT, signal.SIGBUS, signal.SIGILL, signal.SIGFPE, signal.SIGKILL)}


def exit_code(code):
    """A shell wrapper reports a fatal signal as exit 128+N; return it as -N."""
    return 128 - code if code - 128 in FATAL_SIGNALS else code
