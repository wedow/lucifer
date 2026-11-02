"""repr address normalization."""
from support import loops


def run():
    return repr(object()), repr(lambda: None), 'literal 0x1234 survives'


for _ in range(loops()):
    RESULT = run()
print(repr(RESULT))
