import signal

# The default SIGINT handler runs at a poll made at the recursion limit: it
# must not need a frame there, or its KeyboardInterrupt is lost and the
# RecursionError handlers below keep the program running. The signal is
# raised from inside the recursion, so no timing is involved.
calls = 0


def f():
    global calls
    calls += 1
    if calls > 100000:
        raise SystemError("interrupt lost")
    try:
        f()
    except RecursionError:
        signal.raise_signal(signal.SIGINT)
        f()


try:
    f()
except KeyboardInterrupt:
    print("interrupted")
except SystemError as e:
    print(e)
