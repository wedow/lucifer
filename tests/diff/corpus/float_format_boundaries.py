"""float format boundaries."""
from support import loops


def run():
    return [(repr(x), format(x, '.17g'), format(x, '.2f'), format(x, '+020.4e')) for x in (-0.0, 2.675, 999.995, 1e-7, 1e16)]


for _ in range(loops()):
    RESULT = run()
print(repr(RESULT))
