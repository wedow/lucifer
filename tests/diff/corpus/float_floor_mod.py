"""float floor mod."""
from support import loops


def run():
    return [(a, b, a // b, a % b, divmod(a, b)) for a in (-7.5, -0.0, 7.5) for b in (-2.0, 2.0)]


for _ in range(loops()):
    RESULT = run()
print(repr(RESULT))
