"""float signed zero."""
from support import loops


def run():
    import math
    return [(repr(x), x.hex(), math.copysign(1, x)) for x in (0.0, -0.0, -1.0 * 0.0, -0.0 + -0.0)], (-0.0).as_integer_ratio()


for _ in range(loops()):
    RESULT = run()
print(repr(RESULT))
