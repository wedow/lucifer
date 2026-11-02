"""augassign list alias."""
from support import loops


def run():
    a = [1]; b = a; a += [2, 3]
    before = a is b; a *= 2
    return before, a is b, a, b


for _ in range(loops()):
    RESULT = run()
print(repr(RESULT))
