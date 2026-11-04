# A recursion that catches RecursionError and recurses again, ending with a
# SystemError raised at the limit. Building the exception needs frames that
# CPython does not count, so the failures and the final chain match its
# counts. This is signals_deep_recursion without the signal.
calls = 0


def f():
    global calls
    calls += 1
    if calls == 3000:
        raise SystemError("gave up")
    try:
        f()
    except RecursionError:
        f()


try:
    f()
except SystemError as e:
    print("caught", e, calls)
    chain = 0
    while e is not None:
        chain += 1
        e = e.__context__
    print("chain", chain)
