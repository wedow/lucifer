"""sort key mutation."""
from support import loops


def run():
    values = [3, 2, 1]; seen = []
    def key(x):
        seen.append(list(values)); values.append(99); return x
    try: values.sort(key=key)
    except ValueError as e: return values, seen, type(e).__name__, str(e)


for _ in range(loops()):
    RESULT = run()
print(repr(RESULT))
