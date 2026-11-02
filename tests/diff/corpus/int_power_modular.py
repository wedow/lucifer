"""int power modular."""
from support import loops


def run():
    return pow(2, 127), pow(7, 1234, 97), pow(3, -1, 11), pow(3, -2), pow(-2, 101)


for _ in range(loops()):
    RESULT = run()
print(repr(RESULT))
