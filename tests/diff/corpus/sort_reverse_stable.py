"""sort reverse stable."""
from support import loops


def run():
    values = [(i % 2, i) for i in range(8)]
    return sorted(values, key=lambda x: x[0], reverse=True)


for _ in range(loops()):
    RESULT = run()
print(repr(RESULT))
