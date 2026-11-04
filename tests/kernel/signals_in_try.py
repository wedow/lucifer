import _signal
import _thread
import os

_signal.signal(_signal.SIGINT, _signal.default_int_handler)
SIGINT = _signal.SIGINT


# Every case sends the signal and then spins on a back-edge that its try
# covers: under handoff the signal arrives asynchronously, and a poll at an
# uncovered instruction would let it escape on CPython as well.
def spin_until_signal(send):
    send()
    while True:
        pass


def via_raise():
    _signal.raise_signal(SIGINT)


def via_kill():
    os.kill(os.getpid(), SIGINT)


def via_interrupt_main():
    _thread.interrupt_main()


for name, send in (("raise", via_raise), ("kill", via_kill), ("interrupt_main", via_interrupt_main)):
    # try/except around a spinning loop
    try:
        spin_until_signal(send)
    except KeyboardInterrupt:
        print(name, "caught")

    # the try is in the same frame as the loop
    try:
        send()
        while True:
            pass
    except KeyboardInterrupt:
        print(name, "caught in frame")

    # nested try: the inner handler does not match, the outer one does
    try:
        try:
            send()
            while True:
                pass
        except ValueError:
            print("wrong handler")
    except KeyboardInterrupt:
        print(name, "caught by outer")

    # try/finally runs its cleanup and the exception goes on
    log = []
    try:
        try:
            send()
            while True:
                pass
        finally:
            log.append("finally")
    except KeyboardInterrupt:
        log.append("outer")
    print(name, log)

    # with-statement exits
    class CM:
        def __enter__(self):
            return self

        def __exit__(self, t, v, tb):
            print(name, "exit", t.__name__)
            return False

    try:
        with CM():
            send()
            while True:
                pass
    except KeyboardInterrupt:
        print(name, "with caught")

    # a generator's own try
    def gen():
        try:
            for n in range(50):
                yield n
            send()
            while True:
                pass
        except KeyboardInterrupt:
            yield "gen caught"

    g = gen()
    out = None
    for _ in range(100):  # the generator spins in its try after 50 yields
        out = next(g)
        if out == "gen caught":
            break
    print(name, out)

    # a for loop
    try:
        for i in range(3):
            send()
            while True:
                pass
    except KeyboardInterrupt:
        print(name, "for caught")

    # the same try again and again
    hits = 0
    for round in range(3):
        try:
            send()
            while True:
                pass
        except KeyboardInterrupt:
            hits += 1
    print(name, "rounds", hits)


# a signal in a thread other than main goes through the main thread
def alarm_case():
    _signal.signal(_signal.SIGALRM, _signal.default_int_handler)
    _signal.setitimer(_signal.ITIMER_REAL, 0.05)
    try:
        while True:
            pass
    except KeyboardInterrupt:
        print("alarm caught")


alarm_case()


# a thread interrupts a main thread that only spins on back-edges
def thread_case():
    started = _thread.allocate_lock()
    started.acquire()

    def interrupter():
        started.acquire()
        _thread.interrupt_main()

    _thread.start_new_thread(interrupter, ())
    try:
        started.release()
        while True:
            pass
    except KeyboardInterrupt:
        print("thread caught")


thread_case()
