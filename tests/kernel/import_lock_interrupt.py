"""KeyboardInterrupt while _imp.acquire_lock waits: as in CPython
(_PyRecursiveMutex_Lock) the wait is not interrupted, so the exception
arrives with the import lock held (luc-yi7s)."""
import _imp
import _thread
import threading
import time

ready = threading.Event()


def holder():
    _imp.acquire_lock()
    ready.set()
    time.sleep(0.2)            # the main thread now waits in acquire_lock
    _thread.interrupt_main()
    time.sleep(0.2)
    _imp.release_lock()


t = threading.Thread(target=holder)
t.start()
ready.wait()
held = None
try:
    _imp.acquire_lock()
    time.sleep(1)
    print('no interrupt')
except KeyboardInterrupt:
    try:
        _imp.release_lock()
        held = True
    except RuntimeError:
        held = False
t.join()
print('held when interrupted', held, _imp.lock_held())
