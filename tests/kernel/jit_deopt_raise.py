# A function that builds an exception positionally, sets fields on it and
# raises it, called from a handler loop in a function: under every deopt
# stress period the exit after a call that kept its frames matches them.
def mk(t, n, o):
    e = AttributeError(t)
    e.name = n
    e.obj = o
    raise e


def main():
    c = 0
    for i in range(300):
        try:
            mk("x", "y", i)
        except AttributeError as e:
            c += e.obj
            assert e.name == "y" and str(e) == "x"
    return c


print(main())
