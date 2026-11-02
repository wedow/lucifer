"""int floor mod signs."""
from support import loops


def run():
    return [(a, b, a // b, a % b, divmod(a, b)) for a in (-2**90-1, -7, 0, 7, 2**90+1) for b in (-3, 3)]


for _ in range(loops()):
    RESULT = run()
print(repr(RESULT))
