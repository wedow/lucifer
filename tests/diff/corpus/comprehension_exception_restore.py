"""comprehension exception restore."""
from support import loops


def run():
    x = 7
    try: values = [1 // x for x in (2, 1, 0)]
    except ZeroDivisionError: return x, 'caught'


for _ in range(loops()):
    RESULT = run()
print(repr(RESULT))
