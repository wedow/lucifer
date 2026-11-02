"""Uncaught exception chaining and full traceback rendering."""
# difftest-exit: 1
from support import loops


def inner():
    raise KeyError("inner")


def hot():
    try:
        inner()
    except KeyError as error:
        raise ValueError("outer") from error


for _ in range(loops()):
    try:
        hot()
    except ValueError:
        pass
hot()
