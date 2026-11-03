import _signal
import _thread

SIGUSR1 = _signal.SIGUSR1
SIGUSR2 = _signal.SIGUSR2
SIGINT = _signal.SIGINT
got = []


def handler(signum, frame):
    got.append(signum)


print(_signal.signal(SIGUSR1, handler) == _signal.SIG_DFL, _signal.getsignal(SIGUSR1) is handler)

# raise_signal runs the handler before it returns.
_signal.raise_signal(SIGUSR1)
print("raised", got)

# interrupt_main only trips the signal; the handler runs at the next poll,
# which is a call at the latest.
_thread.interrupt_main(SIGUSR1)
print("interrupted", got)

# A handler runs in a tight loop at a back-edge or call, once per signal.
count = 0
for i in range(100):
    if i % 25 == 0:
        _signal.raise_signal(SIGUSR1)
    count += 1
print("loop", count, len(got))


# A handler that raises raises where the signal was handled.
def boom(signum, frame):
    raise ValueError("handler %d" % signum)


_signal.signal(SIGUSR2, boom)
try:
    _signal.raise_signal(SIGUSR2)
except ValueError as e:
    print("boom", e)

# The default SIGINT handler raises KeyboardInterrupt.
_signal.signal(SIGINT, _signal.default_int_handler)
try:
    _thread.interrupt_main()
except KeyboardInterrupt:
    print("keyboard interrupt")

# An ignored signal runs nothing, and so does the default one for interrupt_main.
_signal.signal(SIGUSR1, _signal.SIG_IGN)
before = len(got)
_signal.raise_signal(SIGUSR1)
_thread.interrupt_main(SIGUSR1)
print("ignored", len(got) - before)
for bad in (0, 65, -1):
    try:
        _thread.interrupt_main(bad)
    except ValueError as e:
        print(bad, e)
