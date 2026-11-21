import sys
from collections import _tuplegetter


class Index:
    def __init__(self, value):
        self.value = value

    def __index__(self):
        return self.value


def show(index, doc):
    try:
        getter = _tuplegetter(index, doc)
    except OverflowError as exc:
        print(type(exc).__name__, str(exc))
    else:
        print('accepted', getter.__reduce__()[1])
        print(repr(getter))


show(1, "field's doc\n")
show(Index(0), None)
show(sys.maxsize, ('doc', 1))
show(-sys.maxsize - 1, 0)
show(sys.maxsize + 1, 0)
show(-sys.maxsize - 2, 0)
show(Index(2**64), 0)
