"""The parent's side of os.fork, as CPython's os_fork_impl: the after_in_parent
hooks run even when fork fails (logging's hook gives back the lock its before
hook took), and the multi-threaded warning is only shown, never raised."""
import logging
import os
import resource
import threading
import warnings

log = []
os.register_at_fork(before=lambda: log.append('before'),
                    after_in_parent=lambda: log.append('after_in_parent'))
soft, hard = resource.getrlimit(resource.RLIMIT_NPROC)
resource.setrlimit(resource.RLIMIT_NPROC, (1, hard))      # fork fails with EAGAIN
try:
    if os.fork() == 0:
        os._exit(0)
    print('forked')
except OSError as exc:
    print('fork failed', type(exc).__name__)
finally:
    resource.setrlimit(resource.RLIMIT_NPROC, (soft, hard))
print(log)
names = []
t = threading.Thread(target=lambda: names.append(logging.getLogger('x').name), daemon=True)
t.start()
t.join(5)
print('logging in a thread', names)

stop = threading.Event()
t = threading.Thread(target=stop.wait)
t.start()
with warnings.catch_warnings(record=True) as caught:
    warnings.simplefilter('always')
    pid = os.fork()
    if pid == 0:
        os._exit(0)
os.waitpid(pid, 0)
print([(w.category.__name__, os.path.basename(w.filename), w.lineno) for w in caught])
warnings.simplefilter('error')
pid = os.fork()
if pid == 0:
    os._exit(0)
os.waitpid(pid, 0)
print('error filter: parent got a pid', pid > 0)
stop.set()
t.join()
