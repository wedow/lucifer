"""augassign dict union."""
from support import loops


def run():
    d = {'a': 1, 'b': 2}; alias = d; d |= {'b': 8, 'c': 3}
    return d is alias, list(d.items()), list(alias.items())


for _ in range(loops()):
    RESULT = run()
print(repr(RESULT))
