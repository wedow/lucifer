"""KeyboardInterrupt while os.fork waits for the import lock: CPython takes
and gives back the lock in C around fork(), so the exception arrives in the
parent with the import lock released and imports in other threads go on
(luc-t6nh)."""
import _imp
import _thread
import os
import threading
import time
import warnings

warnings.simplefilter('ignore')
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
print('lock held after fork', _imp.lock_held())
done = []
importer = threading.Thread(target=lambda: done.append(__import__('colorsys').__name__))
importer.start()
importer.join(5)
print('import in a thread', done)
