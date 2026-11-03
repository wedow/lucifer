import _signal
import _thread
import time

n = 0
got = []


def handler(signum, frame):
    global n
    n += 1


_signal.signal(_signal.SIGUSR1, handler)

# raise_signal in a tight loop: every signal is handled before the next call.
for i in range(2000):
    _signal.raise_signal(_signal.SIGUSR1)
print("tight loop", n)


# setitimer interrupts time.sleep: the handler runs, then sleep resumes
# (PEP 475) unless the handler raises.
def alarm(signum, frame):
    got.append(signum)
    raise TimeoutError("timer")


_signal.signal(_signal.SIGALRM, alarm)
_signal.setitimer(_signal.ITIMER_REAL, 0.05)
start = time.monotonic()
try:
    time.sleep(5)
except TimeoutError as e:
    print("sleep interrupted", e, got == [_signal.SIGALRM], time.monotonic() - start < 2)

# A handler that returns lets the sleep run to its full length.
ticks = []
_signal.signal(_signal.SIGALRM, lambda s, f: ticks.append(s))
_signal.setitimer(_signal.ITIMER_REAL, 0.05)
start = time.monotonic()
time.sleep(0.3)
print("sleep resumed", ticks, time.monotonic() - start >= 0.29)
print(_signal.setitimer(_signal.ITIMER_REAL, 0) == (0.0, 0.0))

# interrupt_main from the main thread raises KeyboardInterrupt at the next poll.
_signal.signal(_signal.SIGINT, _signal.default_int_handler)
try:
    _thread.interrupt_main()
    for _ in range(10):
        pass
except KeyboardInterrupt:
    print("interrupt_main")
