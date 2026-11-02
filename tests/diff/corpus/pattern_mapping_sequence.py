"""pattern mapping sequence."""
from support import loops


def run():
    def classify(value):
        match value:
            case {'x': x, **rest}: return ('mapping', x, list(rest.items()))
            case [first, *tail]: return ('sequence', first, tail)
            case _: return ('other', type(value).__name__)
    return [classify(x) for x in ({'x': 3, 'y': 4}, [1, 2, 3], 'abc', [])]


for _ in range(loops()):
    RESULT = run()
print(repr(RESULT))
