# Threads at interpreter exit (FOUNDATIONS 2.13): a non-daemon thread is joined before the exit, a
# daemon thread still running when the interpreter finalizes is left behind, an exception in a
# thread leaves the exit code alone, and joining a daemon thread from a finalizer raises
# PythonFinalizationError instead of hanging. Daemon threads are stopped before the main module is
# torn down, so they never see its globals gone, even while they stop the world. Each child process
# is one exit.
import subprocess
import sys

CHILDREN = {
    "non-daemon joined": '''
import threading, time
def late():
    time.sleep(0.2)
    print("late thread done", flush=True)
threading.Thread(target=late).start()
print("main done", flush=True)
''',
    "daemon running": '''
import threading
def spin():
    while True:
        pass
threading.Thread(target=spin, daemon=True).start()
print("main done", flush=True)
''',
    "daemon blocked on a lock": '''
import threading
lock = threading.Lock()
lock.acquire()
threading.Thread(target=lock.acquire, daemon=True).start()
print("main done", flush=True)
''',
    "thread raises": '''
import threading
def boom():
    raise ValueError("in thread")
t = threading.Thread(target=boom)
t.start()
t.join()
print("main done", flush=True)
''',
    "exit code kept": '''
import sys, threading
t = threading.Thread(target=lambda: None)
t.start()
sys.exit(3)
''',
    "daemon reading globals": '''
import threading
counter = [0]
def count():
    while True:
        counter[0] += 1
for _ in range(2):
    threading.Thread(target=count, daemon=True).start()
print("main done", flush=True)
''',
    "daemons stopping the world": '''
import sys, threading
def stop_the_world():
    while True:
        sys.setrecursionlimit(1000)
for _ in range(4):
    threading.Thread(target=stop_the_world, daemon=True).start()
threading.Event().wait(0.3)
print("main done", flush=True)
''',
    "join daemon in finalizer": '''
import threading
def loop():
    while True:
        pass
class Cycle:
    def __init__(self):
        self.self_ref = self
        self.thr = threading.Thread(target=loop, daemon=True)
        self.thr.start()
    def __del__(self):
        try:
            self.thr.join()
        except PythonFinalizationError:
            print("got the correct exception!", flush=True)
Cycle()
''',
}
for name, code in CHILDREN.items():
    try:
        child = subprocess.run([sys.executable, "-I", "-S", "-c", code], capture_output=True, timeout=30)
        stderr = child.stderr.decode().strip().splitlines()
        print(name, child.returncode, child.stdout.decode().split("\n"), stderr[-1:] if stderr else [])
    except subprocess.TimeoutExpired:
        print(name, "hung")
