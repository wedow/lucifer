"""A with statement whose __enter__ is a method descriptor of another type
(list.copy on a plain class): the call raises the descriptor's TypeError,
in the interpreter and in compiled code alike, every time."""
from support import loops


class C:
    __enter__ = list.copy

    def __exit__(self, *a):
        return False


class Ok:
    __enter__ = list.copy

    def __exit__(self, *a):
        return False


def f():
    try:
        with C():
            pass
    except TypeError as e:
        return str(e)
    return 'no error'


def run():
    return [f() for _ in range(300)][-3:]


for _ in range(loops()):
    RESULT = run()
print(repr(RESULT))
