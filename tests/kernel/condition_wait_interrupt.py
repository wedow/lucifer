"""KeyboardInterrupt while Condition.wait takes its lock back: as in
CPython the wait takes it back first, so the exception leaves the with
block holding and releasing the lock cleanly (luc-otbd)."""
import threading, time, _thread
cond = threading.Condition()
ready = threading.Event()
def helper():
    ready.wait()
    with cond:
        cond.notify()
        time.sleep(0.2)        # the main thread now waits for the lock in wait()
        _thread.interrupt_main()
        time.sleep(0.2)
try:
    with cond:
        t = threading.Thread(target=helper); t.start()
        ready.set()
        cond.wait(10)
        print("waited")
except BaseException as e:
    print(type(e).__name__, type(e.__context__).__name__ if e.__context__ else None)
t.join()
print("locked", cond._lock.locked())
