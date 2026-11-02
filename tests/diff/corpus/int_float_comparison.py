"""int float comparison."""
from support import loops


def run():
    values = [2**53-1, 2**53, 2**53+1, 2**100+1]
    return [(x == float(x), x < float(x), x > float(x)) for x in values]


for _ in range(loops()):
    RESULT = run()
print(repr(RESULT))
