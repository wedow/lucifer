"""A signal pending as os.fork returns in the parent, as CPython's os_fork_impl:
the parent's after_in_parent hooks run all the same, and the handler fires in
the first of them, which reports it through sys.unraisablehook ('Exception
ignored in atfork callback'), and the rest still run (luc-livv)."""
import _imp
import _thread
import os
import sys
import threading
import time
import warnings

warnings.simplefilter('ignore')
log = []


class Hook:
    def __init__(self, name):
        self.name = name

    def __call__(self):
        log.append(self.name)

    def __repr__(self):
        return '<hook %s>' % self.name


sys.unraisablehook = lambda u: print('unraisable:', u.err_msg, type(u.exc_value).__name__)
os.register_at_fork(after_in_parent=Hook('first'))
os.register_at_fork(after_in_parent=Hook('second'))
ready = threading.Event()


def holder():
    _imp.acquire_lock()
    ready.set()
    time.sleep(0.2)            # the main thread now waits in fork for the import lock
    _thread.interrupt_main()
    time.sleep(0.2)
    _imp.release_lock()


t = threading.Thread(target=holder)
t.start()
ready.wait()
try:
    if os.fork() == 0:
        os._exit(0)
    print('no interrupt')
except KeyboardInterrupt:
    print('interrupted')
t.join()
os.waitpid(-1, 0)
print(log)
