"""sort stability."""
from support import loops


def run():
    values = [(i % 3, i) for i in range(12)]
    values.sort(key=lambda item: item[0])
    return values


for _ in range(loops()):
    RESULT = run()
print(repr(RESULT))
