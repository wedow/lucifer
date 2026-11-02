"""conversion int bases."""
from support import loops


def run():
    return [(s, base, int(s, base)) for s, base in [('0xff', 0), (' -0b101 ', 0), ('1_000', 10), ('٣٤', 10), ('zz', 36)]], int(True), int(-3.9)


for _ in range(loops()):
    RESULT = run()
print(repr(RESULT))
