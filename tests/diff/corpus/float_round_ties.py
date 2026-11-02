"""float round ties."""
from support import loops


def run():
    return [(x, round(x), round(x, 1), round(x, -1)) for x in (0.5, 1.5, 2.5, -0.5, -1.5, 25.0, 2.675)]


for _ in range(loops()):
    RESULT = run()
print(repr(RESULT))
