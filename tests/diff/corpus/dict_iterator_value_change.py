"""dict iterator value change."""
from support import loops


def run():
    d = {'a': 1, 'b': 2}; out = []
    for k in d:
        d[k] *= 10; out.append((k, d[k]))
    return out, list(d.items())


for _ in range(loops()):
    RESULT = run()
print(repr(RESULT))
