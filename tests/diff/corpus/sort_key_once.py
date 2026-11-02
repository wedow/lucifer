"""sort key once."""
from support import loops


def run():
    calls = []; values = [3, 1, 2, 1]
    def key(x): calls.append(x); return x % 2
    values.sort(key=key)
    return values, calls


for _ in range(loops()):
    RESULT = run()
print(repr(RESULT))
