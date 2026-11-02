"""closure late binding."""
from support import loops


def run():
    fs = [lambda: i for i in range(5)]
    gs = [lambda i=i: i for i in range(5)]
    return [f() for f in fs], [g() for g in gs]


for _ in range(loops()):
    RESULT = run()
print(repr(RESULT))
