"""closure recursive capture."""
from support import loops


def run():
    def outer():
        def f(n): return 1 if n <= 1 else n * f(n - 1)
        return f
    return outer()(12)


for _ in range(loops()):
    RESULT = run()
print(repr(RESULT))
