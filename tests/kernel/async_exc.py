import ctypes
import threading

# PyThreadState_SetAsyncExc through ctypes.pythonapi, which keeps the thread
# attached: a thread spinning in a try is stopped by the exception it is sent.


class Stop(Exception):
    pass


ready = threading.Event()
result = []


def spin():
    try:
        ready.set()
        while True:
            pass
    except Stop:
        result.append("stopped")
    finally:
        result.append("finally")


t = threading.Thread(target=spin)
t.start()
ready.wait()
n = ctypes.pythonapi.PyThreadState_SetAsyncExc(ctypes.c_ulong(t.ident), ctypes.py_object(Stop))
t.join()
print(n, result)

# the main thread raises it in itself at its next poll
try:
    ctypes.pythonapi.PyThreadState_SetAsyncExc(ctypes.c_ulong(threading.get_ident()), ctypes.py_object(Stop))
    while True:
        pass
except Stop:
    print("main stopped")
print(ctypes.pythonapi.PyThreadState_SetAsyncExc(ctypes.c_ulong(0), ctypes.py_object(Stop)))
