# A module body that forks, and a child that does not exit but carries on: it finishes the program
# the parent started, with the output flushed, the atexit handlers run and the exit status the
# program chooses. (Under -X lucifer_handoff the module body can run on the second carrier, where
# the child has no thread that called the program to return its result to; the thread it has is
# still its main thread, which installs signal handlers.)
import atexit
import os
import sys
import tempfile
import shutil

d = tempfile.mkdtemp()
with open(d + "/forker.py", "w") as f:
    f.write("""import os
pid = os.fork()
is_child = pid == 0
child_status = None
if not is_child:
    child_status = os.waitstatus_to_exitcode(os.waitpid(pid, 0)[1])
""")
sys.path.insert(0, d)
import forker
shutil.rmtree(d, ignore_errors=True) if not forker.is_child else None
atexit.register(lambda: print("atexit", "child" if forker.is_child else "parent"))
if forker.is_child:
    import signal
    import threading
    signal.signal(signal.SIGUSR1, signal.SIG_IGN)       # the child's one thread is its main thread
    print("child carries on", threading.main_thread().ident == threading.get_ident())
    sys.exit(3)
print("parent sees child status", forker.child_status)
