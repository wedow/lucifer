import atexit
import sys


class Noisy:
    def __init__(self, name):
        self.name = name

    def __del__(self):
        print("del", self.name)


def first():
    print("atexit first")


def second(a, b=0):
    print("atexit second", a, b)


class Boom:
    def __repr__(self):
        return "<boom>"

    def __call__(self):
        print("atexit boom")
        raise ValueError("from atexit")


boom = Boom()


atexit.register(first)
atexit.register(second, 1, b=2)
atexit.register(boom)
atexit.register(lambda: print("atexit last registered, run first"))
atexit.unregister(first)

keep = Noisy("module global")
local = Noisy("collected now")
del local
print("exiting")
sys.stdout.flush()
sys.exit(3)
