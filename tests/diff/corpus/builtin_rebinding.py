"""builtin rebinding."""
from support import loops


def run():
    namespace = {}
    exec('def f(x): return len(x)', namespace)
    f = namespace['f']
    for _ in range(loops()): before = f([1, 2])
    namespace['len'] = lambda x: 99
    return before, f([1, 2])


for _ in range(loops()):
    RESULT = run()
print(repr(RESULT))
