"""container state types."""
# difftest-state
from support import loops


def run():
    a = [True, 1, 1.0, -0.0, b'abc', bytearray(b'xyz')]
    return {'a': a, 'tuple': (a,), 'set': {3, 1, 2}, 'frozen': frozenset({'b', 'a'})}


for _ in range(loops()):
    RESULT = run()
STATE = RESULT
print("state captured")
