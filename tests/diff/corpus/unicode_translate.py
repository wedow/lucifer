"""unicode translate."""
from support import loops


def run():
    table = str.maketrans({'a': 'XYZ', 'é': None, '😀': 'x'})
    return 'aé😀b'.translate(table), 'abracadabra'.replace('a', 'x', 3), ''.replace('', '-')


for _ in range(loops()):
    RESULT = run()
print(repr(RESULT))
