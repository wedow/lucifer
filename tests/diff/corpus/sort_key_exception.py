"""sort key exception."""
from support import loops


def run():
    values = [4, 3, 2, 1]; calls = []
    def key(x):
        calls.append(x)
        if x == 2: raise LookupError('key')
        return x
    try: values.sort(key=key)
    except LookupError as e: return values, calls, e.args


for _ in range(loops()):
    RESULT = run()
print(repr(RESULT))
