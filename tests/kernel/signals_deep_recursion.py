import _thread
import time
import threading

# The default SIGINT handler runs at a poll made at the recursion limit: it
# must not need a frame there, or its KeyboardInterrupt is lost and the
# RecursionError handlers below keep the program running.
gave_up = False


def f():
    if gave_up:
        raise SystemError("interrupt lost")
    try:
        f()
    except RecursionError:
        f()


def interrupt():
    time.sleep(0.3)
    _thread.interrupt_main()


def watchdog():
    global gave_up
    time.sleep(5)
    gave_up = True


threading.Thread(target=interrupt).start()
threading.Thread(target=watchdog, daemon=True).start()
try:
    f()
except KeyboardInterrupt:
    print("interrupted")
except SystemError as e:
    print(e)
